/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*********************************************************************
**
** NAME:
**   ux-dialog.cc
**
** DESCRIPTION:
**   Netscape Directory Server Pre-installation Program
**   Definitions for UI dialogs.
**
** NOTES:
**
**
*********************************************************************/

#include <errno.h>
#include <iostream.h>
#include <fstream.h>
/* Newer g++ wants the new std header forms */
#if defined( Linux )
#include <strstream>
using std::ostrstream;
/* But some platforms won't accept those (specifically HP-UX aCC */
#else
#include <strstream.h>
#endif
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "utf8.h"
#include "ux-util.h"
#include "dialog.h"
#include "ux-dialog.h"
#include "ux-config.h"
#include "install_keywords.h"
extern "C" {
#include "dsalib.h"
}

static const char *DEFAULT_SLAPDUSER = "cn=Directory Manager";

// #define DEBUG 2

/*
** Forward References
*/

static DialogAction yesNoDefaultNo (const char *answer);
static DialogAction askReconfigNext (Dialog *me);
static DialogAction askSlapdServerNameSetup (Dialog *me);
static DialogAction askSlapdServerNameNext(Dialog *me);
static DialogAction askAdminPortSetup (Dialog *me);
static DialogAction askAdminPortNext(Dialog *me);
static DialogAction askSlapdPortSetup (Dialog *me);
static DialogAction askSlapdPortNext(Dialog *me);
static DialogAction askSecurityNext (Dialog *me);
static DialogAction askSlapdSecPortSetup (Dialog *me);
static DialogAction askSlapdSecPortNext(Dialog *me);
static DialogAction askSlapdServerIDSetup (Dialog *me);
static DialogAction askSlapdServerIDNext(Dialog *me);
static DialogAction askSr2xInfoSetup(Dialog *me);
static DialogAction askSr2xInfoNext(Dialog *me);
static DialogAction askSlapdRootDNSetup(Dialog *me);
static DialogAction askSlapdRootDNNext (Dialog *me);
static DialogAction askSlapdSysUserSetup (Dialog *me);
static DialogAction askSlapdSysUserNext (Dialog *me);
static DialogAction askConfigForMCNext (Dialog *me);
static DialogAction askMCAdminIDSetup (Dialog *me);
static DialogAction askMCAdminIDNext (Dialog *me);
static DialogAction askReconfigMCAdminPwdSetup (Dialog *me);
static DialogAction askReconfigMCAdminPwdNext (Dialog *me);
static DialogAction askSlapdSuffixSetup (Dialog *me);
static DialogAction askSlapdSuffixNext (Dialog *me);
static DialogAction askSampleSetup (Dialog *me);
static DialogAction askSampleNext (Dialog *me);
static DialogAction askPopulateSetup (Dialog *me);
static DialogAction askPopulateNext (Dialog *me);
static DialogAction askOrgSizeSetup (Dialog *me);
static DialogAction askOrgSizeNext (Dialog *me);
static DialogAction askCIRSetup(Dialog *me);
static DialogAction askCIRNext(Dialog *me);
static DialogAction askCIRHostSetup(Dialog *me);
static DialogAction askCIRHostNext(Dialog *me);
static DialogAction askCIRPortSetup(Dialog *me);
static DialogAction askCIRPortNext(Dialog *me);
static DialogAction askCIRDNSetup(Dialog *me);
static DialogAction askCIRDNNext(Dialog *me);
static DialogAction askCIRSuffixSetup(Dialog *me);
static DialogAction askCIRSuffixNext(Dialog *me);
static DialogAction askCIRSSLSetup(Dialog *me);
static DialogAction askCIRSSLNext(Dialog *me);
static DialogAction askCIRIntervalSetup(Dialog *me);
static DialogAction askCIRIntervalNext(Dialog *me);
static DialogAction askCIRDaysSetup(Dialog *me);
static DialogAction askCIRDaysNext(Dialog *me);
static DialogAction askCIRTimesSetup(Dialog *me);
static DialogAction askCIRTimesNext(Dialog *me);
static DialogAction askSIRSetup(Dialog *me);
static DialogAction askSIRNext(Dialog *me);
static DialogAction askChangeLogSuffixSetup(Dialog *me);
static DialogAction askChangeLogSuffixNext(Dialog *me);
static DialogAction askChangeLogDirSetup(Dialog *me);
static DialogAction askChangeLogDirNext(Dialog *me);
static DialogAction askReplicationDNSetup(Dialog *me);
static DialogAction askReplicationDNNext(Dialog *me);
static DialogAction askReplicationSetup(Dialog *me);
static DialogAction askReplicationNext(Dialog *me);
static DialogAction askConsumerDNSetup(Dialog *me);
static DialogAction askConsumerDNNext(Dialog *me);
static DialogAction askSIRHostSetup(Dialog *me);
static DialogAction askSIRHostNext(Dialog *me);
static DialogAction askSIRPortSetup(Dialog *me);
static DialogAction askSIRPortNext(Dialog *me);
static DialogAction askSIRDNSetup(Dialog *me);
static DialogAction askSIRDNNext(Dialog *me);
static DialogAction askSIRSuffixSetup(Dialog *me);
static DialogAction askSIRSuffixNext(Dialog *me);
static DialogAction askSIRSSLSetup(Dialog *me);
static DialogAction askSIRSSLNext(Dialog *me);
static DialogAction askSIRDaysSetup(Dialog *me);
static DialogAction askSIRDaysNext(Dialog *me);
static DialogAction askSIRTimesSetup(Dialog *me);
static DialogAction askSIRTimesNext(Dialog *me);
static DialogAction askUseExistingMCSetup(Dialog *me);
static DialogAction askUseExistingMCNext(Dialog *me);
static DialogAction askMCHostSetup(Dialog *me);
static DialogAction askMCHostNext(Dialog *me);
static DialogAction askMCPortSetup(Dialog *me);
static DialogAction askMCPortNext(Dialog *me);
static DialogAction askMCDNSetup(Dialog *me);
static DialogAction askMCDNNext(Dialog *me);
static DialogAction askDisableSchemaCheckingSetup(Dialog *me);
static DialogAction askDisableSchemaCheckingNext(Dialog *me);
static DialogAction askMCAdminDomainSetup(Dialog *me);
static DialogAction askMCAdminDomainNext(Dialog *me);
static DialogAction askAdminDomainSetup(Dialog *me);
static DialogAction askAdminDomainNext(Dialog *me);
static DialogAction askUseExistingUGSetup(Dialog *me);
static DialogAction askUseExistingUGNext(Dialog *me);
static DialogAction askUGHostSetup(Dialog *me);
static DialogAction askUGHostNext(Dialog *me);
static DialogAction askUGPortSetup(Dialog *me);
static DialogAction askUGPortNext(Dialog *me);
static DialogAction askUGDNSetup(Dialog *me);
static DialogAction askUGDNNext(Dialog *me);
static DialogAction askUGSuffixSetup(Dialog *me);
static DialogAction askUGSuffixNext(Dialog *me);

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
			!*rdnNoTypes[0] || !strcasecmp(rdnList[0], rdnNoTypes[0]))
		{
			ret = 0;
		}
		if (rdnList)
			ldap_value_free(rdnList);
		if (rdnNoTypes)
			ldap_value_free(rdnNoTypes);
	}

	if ((ret == 1) && ds_dn_uses_LDAPv2_quoting(dn_to_test))
	{
		char *newdn = strdup(dn_to_test);
		dn_normalize_convert(newdn);
		char *oldlocaldn = UTF8ToLocal(dn_to_test);
		char *newlocaldn = UTF8ToLocal(newdn);
		free(newdn);
		NSString msg = NSString(
			"The given value [") + oldlocaldn + "] is quoted in the deprecated LDAPv2 style\n" +
			"quoting format.  It will be automatically converted to use the\n" +
			"LDAPv3 style escaped format [" + newlocaldn + "].";
		DialogManagerType::showAlert(msg);
		nsSetupFree(oldlocaldn);
		nsSetupFree(newlocaldn);
	}

	return ret;
}

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

static int
rootDNPwdIsValid(const char *pwd)
{
	if (!pwd || !*pwd || (strlen(pwd) < 8))
		return 0;

	return !contains8BitChars(pwd);
}

static int
isValid(const char *s)
{
	if (!s)
		return 1; // null is a valid response (means to accept default)

	int ret = 1;

	char *ncs = (char *)s; // cast away const-ness for ldaputf8 stuff
	// trim spaces from the beginning of the string
	while (*ncs && ldap_utf8isspace(ncs))
		LDAP_UTF8INC(ncs);

	if (!*ncs) // empty string or all spaces
		ret = 0;

    return ret;
}

static int
isValidServerID(const char *s)
{
	if (!s || !*s)
		return 0;

	if (!isValid(s))
		return 0;

	if (contains8BitChars(s))
		return 0;

	// server ID should contain alphanum, _, -, . since it will
	// be used for both a filename and a DN component
	const char *badChars = "`~!@#$%^&*()[]|\\\"\':;,+=/<>?";
	const char *p = s;
	for (; *p && !strchr(badChars, *p); ++p)
		;

	if (!*p) // the string contains all valid chars
		return 1;

	return 0;
}

static int
isValidYesNo(const char *s)
{
	if (!s)
		return 1; // null means accept default

	const char *msg = 0;
	if (isValid(s))
	{
		int len = strlen(s);
		if (strncasecmp(s, "yes", len) && strncasecmp(s, "no", len))
		{
			msg = "Please type yes or no.";
		}
	}
	else
	{
		msg = "Please specify a valid string.";
	}

	if (msg)
	{
		DialogManagerType::showAlert(msg);
		return 0;
	}

	return 1;
}

static DialogAction
yesNoDefaultNo(const char *answer)
{
   if (answer[0] == '\0' || answer[0] == '\n')
      return DIALOG_EXIT;
   else if (answer[0] != 'y' && answer[0] != 'Y')
      return DIALOG_EXIT;
   else
      return DIALOG_NEXT;
}

static int
dialogSetup (Dialog *me, const char *which, const char *defaultAns)
{
	const char *ans = getManager(me)->getDefaultScript()->get(which);
	if (!ans)
		ans = getManager(me)->getAdminScript()->get(which);
	if (!ans)
		ans = getManager(me)->getBaseScript()->get(which);
	
	int status;
	if (ans == NULL)
		status = 0;
	else
		status = 1;
/*
	int status = (int)ans; // 0 - there was already a value in the script
						   // not zero - no value already in script
*/
	if (ans)
		me->setDefaultAns(ans);
	else if (defaultAns)
		me->setDefaultAns(defaultAns);

	return status;
}

DialogInput askSlapdPort(
"The standard directory server network port number is 389.  However, if\n"
"you are not logged as the superuser, or port 389 is in use, the\n"
"default value will be a random unused port number greater than 1024.\n"
"If you want to use port 389, make sure that you are logged in as the\n"
"superuser, that port 389 is not in use, and that you run the admin\n"
"server as the superuser.\n",

"Directory server network port",

NULL,

askSlapdPortSetup,
askSlapdPortNext
);

static DialogAction
askSlapdPortSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askSlapdPortSetup" << endl;
#endif
	char tmp[10];
	int port = 389;
	const char *defPort =
		getManager(me)->getDefaultScript()->get(SLAPD_KEY_SERVER_PORT);

	if (defPort && *defPort && atoi(defPort) > 0)
	{
		strcpy(tmp, defPort);
		port = atoi(defPort);
	}
	else
		sprintf(tmp, "%d", port);

	// see if default port is available
	if (InstUtil::portAvailable(port) == False)
	{
		// start with a random port number, and keep going until we find
		// an available port
		int origport = port = InstUtil::guessPort();
		while (InstUtil::portAvailable(port) == False)
		{
			++port;
			if (port > MAXPORT)
				port = MINPORT;
			if (port == origport)
			{
				port = -1; // NO AVAILABLE PORTS!!!!!!!
				break;
			}
		}
	}

	if (port == -1) // NO AVAILABLE PORTS!!!!!!!
	{
#if DEBUG > 1
		cerr << "Leaving askSlapdPortSetup DIALOG_ERROR" << endl;
#endif
		return DIALOG_ERROR;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SERVER_PORT, (long)port);

	dialogSetup(me, SLAPD_KEY_SERVER_PORT, tmp);

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askSlapdPortSetup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

#if DEBUG > 1
	cerr << "Leaving askSlapdPortSetup DIALOG_SAME" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askSlapdPortNext(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askSlapdPortNext" << endl;
#endif
	const char *buf = me->input();
	const char *tmp;
	char testbuf[1024];
	int port, err = 0;

	if (buf[0] == 0)
	{
		tmp = me->defaultAns();
	}
	else
	{
		tmp = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SERVER_PORT, tmp);

	port = atoi(tmp);
	sprintf(testbuf, "%d", port);
	if (strncmp(testbuf, tmp, 6) || port > MAXPORT || port < 1)
	{
		sprintf(testbuf, "OVERFLOW ERROR: Unable to bind to port %d\n"
				"Please choose another port between 1 and %d.\n\n",
				port, MAXPORT);
		err = -1;
	} 
	else if (InstUtil::portAvailable(port) == False)
	{
		sprintf(testbuf, "ERROR: Unable to bind to port %d\n"
				"Please choose another port.\n\n", port);
		err = -1;
	}

	if (err)
	{
		DialogManagerType::showAlert(testbuf);
		return DIALOG_SAME;
	}

#if DEBUG > 1
	cerr << "Leaving askSlapdPortNext" << endl;
#endif
	return DIALOG_NEXT;
}

DialogInput askSlapdServerID(
"Each instance of a directory server requires a unique identifier.\n"
"Press Enter to accept the default, or type in another name and press\n"
"Enter.\n",

"Directory server identifier",

NULL,

askSlapdServerIDSetup,
askSlapdServerIDNext
);

static DialogAction
askSlapdServerIDSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askSlapdServerIDSetup" << endl;
#endif
	// extract the hostname part of the FQDN
	const char *tmp = 0;
	char *basehost = 0;
	if (tmp = getManager(me)->getBaseScript()->get(SLAPD_KEY_FULL_MACHINE_NAME)) {
		basehost = strdup(tmp);
	} else {
		basehost = strdup(InstUtil::guessHostname());
	}
	if (!basehost)
		return DIALOG_ERROR;
	char *ptr = strchr(basehost, '.');
	if (ptr)
	{
		*ptr = 0;
	}
	else
	{
		free(basehost);
		basehost = 0;
	}

	const char *ans =
		getManager(me)->getDefaultScript()->get(SLAPD_KEY_SERVER_IDENTIFIER);

	if (!ans && basehost)
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SERVER_IDENTIFIER,
												basehost);
	else if (!ans && !basehost)
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SERVER_IDENTIFIER,
												InstUtil::guessHostname());

	if (ans)
	{
		me->setDefaultAns(ans);
	}
	else if (basehost)
	{
		me->setDefaultAns(basehost);
	}
	else
	{
		me->setDefaultAns(InstUtil::guessHostname());
	}

	if (basehost)
		free(basehost);

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askSlapdServerIDSetup DIALOG_SAME" << endl;
#endif
		return DIALOG_NEXT;
	}

#if DEBUG > 1
	cerr << "Leaving askSlapdServerIDSetup DIALOG_SAME" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askSlapdServerIDNext(Dialog *me)
{
	const char *ans =
		getManager(me)->getDefaultScript()->get(SLAPD_KEY_SERVER_IDENTIFIER);

	const char *buf = me->input();
	const char *tmp;
	char testbuf[1024];
	int err = 0;

	if (buf[0] == 0)
	{
		tmp = me->defaultAns();
	}
	else
	{
		tmp = buf;
	}

	if (!tmp)
	{
		err = -1;
		sprintf(testbuf, "The name must not be empty");
	}
	else if (!isValid(tmp))
	{
		err = -1;
		sprintf(testbuf, "Please specify a valid value for the name.");
	}
	else if (contains8BitChars(tmp))
	{
		err = -1;
		sprintf(testbuf, "The server ID must contain 7 bit ascii only.");
	}
	else if (!isValidServerID(tmp))
	{
		err = -1;
		sprintf(testbuf, "The server ID must be a valid filename and DN component.");
	}

	if (!err)
	{
		// see if an instance by the same name already exists

		NSString instanceDir = NSString(
			getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ROOT)
			) + "/slapd-" + tmp;
		if (InstUtil::fileExists(instanceDir))
		{
			sprintf(testbuf, "ERROR: a server instance named [%s] already exists."
					"  Please choose a unique name.\n", tmp);
			err = -1;
		}
	}				   

	if (tmp)
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SERVER_IDENTIFIER, tmp);

	if (err)
	{
		DialogManagerType::showAlert(testbuf);
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askMCAdminID(
"Please enter the administrator ID for the Brandx configuration\n"
"directory server.  This is the ID typically used to log in to the\n"
"console.  You will also be prompted for the password.\n",

"Brandx configuration directory server\nadministrator ID",

"admin",

askMCAdminIDSetup,
askMCAdminIDNext
);

static DialogAction
askMCAdminIDSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askMCAdminIDSetup" << endl;
#endif
	if (getManager(me)->getAdminScript() &&
		getManager(me)->getAdminScript()->get(SLAPD_KEY_ADMIN_SERVER_ID) &&
		getManager(me)->getAdminScript()->get(SLAPD_KEY_ADMIN_SERVER_PWD))
	{
		// see if the MC Admin ID has been provided
		if (getManager(me)->getBaseScript() &&
			!(getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID) &&
			  getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_PWD)))
		{
			getManager(me)->getBaseScript()->set(
				SLAPD_KEY_SERVER_ADMIN_ID,
				getManager(me)->getAdminScript()->get(SLAPD_KEY_ADMIN_SERVER_ID)
			);
			getManager(me)->getBaseScript()->set(
				SLAPD_KEY_SERVER_ADMIN_PWD,
				getManager(me)->getAdminScript()->get(SLAPD_KEY_ADMIN_SERVER_PWD)
			);
		}
	}

	dialogSetup(me, SLAPD_KEY_SERVER_ADMIN_ID, "admin");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askMCAdminIDSetup setup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

	// this dialog is only used for creating the MC Admin; don't use it if
	// we will be using an existing MC i.e. we are not creating the MC host
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
	{
#if DEBUG > 1
		cerr << "Leaving askMCAdminIDSetup DIALOG_NEXT" << endl;
#endif
		return action;
	}

#if DEBUG > 1
	cerr << "Leaving askMCAdminIDSetup DIALOG_SAME" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askMCAdminIDNext(Dialog *me)
{
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askMCAdminIDNext setup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

	const char *adminUser;
	const char *adminPwd;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		adminUser = me->defaultAns();
	}
	else
	{
		adminUser = buf;
	}

	if (!isValid(adminUser))
	{
		DialogManagerType::showAlert("Please enter a valid ID.");
		return DIALOG_SAME;
	}
	else if (!isAValidDN(adminUser) && contains8BitChars(adminUser))
	{
		DialogManagerType::showAlert("The user ID value must be 7 bit ASCII only.");
		return DIALOG_SAME;
	}

	getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_ID, adminUser);

	while (1)
	{
//		cerr << "before password in askMCAdminIDNext" << endl;
		me->showString("Password: ");
//		cerr << "after password in askMCAdminIDNext" << endl;
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				continue;
			}
			else if (contains8BitChars(inp))
			{
				DialogManagerType::showAlert("Password must contain 7 bit characters only.");
				return DIALOG_SAME;
			}
			else if (!isValid(inp))
			{
				DialogManagerType::showAlert("Please enter a valid password.");
				return DIALOG_SAME;
			}
			else
			{
				me->showString("Password (again): ");
				if (me->getPassword() == 0)
				{
					return DIALOG_PREV;
				}
				else
				{
					adminPwd = me->input();
					if (strcmp(inp,adminPwd))
					{
						DialogManagerType::showAlert("Passwords don't match.");
						return DIALOG_SAME;
					}
					break;
				}
			}
			free(inp);
		}
	}
	getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_PWD, adminPwd);
	return DIALOG_NEXT;
}

DialogInput askSlapdSuffix(
"The suffix is the root of your directory tree.  You may have more than\n"
"one suffix.\n",

"Suffix",

NULL,

askSlapdSuffixSetup,
askSlapdSuffixNext
);

static DialogAction
askSlapdSuffixSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return action;

	if (!getManager(me)->getDefaultScript()->get(SLAPD_KEY_SUFFIX)) {
		getManager(me)->getDefaultScript()->set(
			SLAPD_KEY_SUFFIX, getManager(me)->getDefaultSuffix());
	}

	dialogSetup(me, SLAPD_KEY_SUFFIX, getManager(me)->getDefaultSuffix());

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSlapdSuffixNext(Dialog *me)
{
	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SUFFIX, val);

	// check the value to see if it is a valid DN
	if (!isAValidDN(val))
	{
	 	DialogManagerType::showAlert("A suffix must be a valid DN.");
		return DIALOG_SAME;
	}
	else if (!isValid(val))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askSlapdRootDN(
"Certain directory server operations require an administrative user.\n"
"This user is referred to as the Directory Manager and typically has a\n"
"bind Distinguished Name (DN) of cn=Directory Manager.  Press Enter to\n"
"accept the default value, or enter another DN.  In either case, you\n"
"will be prompted for the password for this user.  The password must\n"
"be at least 8 characters long.\n",

"Directory Manager DN",

DEFAULT_SLAPDUSER,

askSlapdRootDNSetup,
askSlapdRootDNNext
);

static DialogAction
askSlapdRootDNSetup(Dialog *me)
{
	if (!getManager(me)->getDefaultScript()->get(SLAPD_KEY_ROOTDN))
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_ROOTDN,
												DEFAULT_SLAPDUSER);

	dialogSetup(me, SLAPD_KEY_ROOTDN, DEFAULT_SLAPDUSER);

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSlapdRootDNNext(Dialog *me)
{
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askSlapdRootDNNext setup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

	const char *slapdUser;
	const char *slapdPwd;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		slapdUser = me->defaultAns();
	}
	else
	{
		slapdUser = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_ROOTDN, slapdUser);

	// check the value to see if it is a valid DN
	if (!isAValidDN(slapdUser))
	{
	 	DialogManagerType::showAlert("The Directory Manager must be a valid DN.");
	 	return DIALOG_SAME;
	}
	else if (!isValid(slapdUser))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	while (1)
	{
//		cerr << "before password in askSlapdRootDNNext" << endl;
		me->showString("Password: ");
//		cerr << "after password in askSlapdRootDNNext" << endl;
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				continue;
			}
			else if (contains8BitChars(inp))
			{
				DialogManagerType::showAlert("Password must contain 7 bit characters only.");
				return DIALOG_SAME;
			}
			else if (!isValid(inp))
			{
				DialogManagerType::showAlert("Please enter a valid password.");
				return DIALOG_SAME;
			}
			else
			{
				me->showString("Password (again): ");
				if (me->getPassword() == 0)
				{
					return DIALOG_PREV;
				}
				else
				{
					slapdPwd = me->input();
					if (strcmp(inp,slapdPwd))
					{
						DialogManagerType::showAlert("Passwords don't match.");
						return DIALOG_SAME;
					}
					else if (!rootDNPwdIsValid(inp))
					{
						DialogManagerType::showAlert("Password must be at least 8 characters long");
						return DIALOG_SAME;
					}
					break;
				}
			}
			free(inp);
		}
	}
	getManager(me)->getDefaultScript()->set(SLAPD_KEY_ROOTDNPWD, slapdPwd);
	return DIALOG_NEXT;
}

DialogYesNo askSample(
"You may install some sample entries in this directory instance.  These\n"
"entries will be installed in a separate suffix and will not interfere\n"
"with the normal operation of the directory server.\n",

"Do you want to install the sample entries?",

"No",

askSampleSetup,
askSampleNext
);

static DialogAction
askSampleSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSampleNext(Dialog *me)
{
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_ADD_SAMPLE_ENTRIES, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

	return DIALOG_NEXT;
}

DialogYesNo askPopulate(
"You may wish to populate your new directory instance with some data.\n"
"You may already have a file in LDIF format to use or some suggested\n"
"entries can be added.  If you want to import entries from an LDIF\n"
"file, you may type in the full path and filename at the prompt.  If\n"
"you want the install program to add the suggested entries, type the\n"
"word suggest at the prompt.  The suggested entries are common\n"
"container entries under your specified suffix, such as ou=People and\n"
"ou=Groups, which are commonly used to hold the entries for the persons\n"
"and groups in your organization.  If you do not want to add any of\n"
"these entries, type the word none at the prompt.\n",

"Type the full path and filename, the word suggest, or the word none\n",

"none",

askPopulateSetup,
askPopulateNext
);

static DialogAction
askPopulateSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER))
		return action;

	// if setting up a UG host, by default setup the suggested entries
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
	{
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_INSTALL_LDIF_FILE,
												"suggest");
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_ADD_ORG_ENTRIES,
												"Yes");
	}

	dialogSetup(me, SLAPD_KEY_INSTALL_LDIF_FILE, "none");
	me->setInputLen(1024); // it seems to get reset somewhere . . .

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askPopulateNext(Dialog *me)
{
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER))
		return DIALOG_NEXT;

	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	if (buf && !strncasecmp(buf, "none", strlen(buf)))
	{
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_ADD_ORG_ENTRIES, "No");
		getManager(me)->getDefaultScript()->
			set(SLAPD_KEY_INSTALL_LDIF_FILE, "none");
	}
	else if (buf && !strncasecmp(buf, "suggest", strlen(buf)))
	{
		getManager(me)->getDefaultScript()->
			set(SLAPD_KEY_INSTALL_LDIF_FILE, "suggest");
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_ADD_ORG_ENTRIES, "Yes");
	} else {
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_INSTALL_LDIF_FILE, buf);
		if (!InstUtil::fileExists(buf))
		{
			NSString msg = NSString("The specified filename ") + buf + "\n" +
				"does not exist.  Please try again.\n";
			DialogManagerType::showAlert(msg);
			return DIALOG_SAME;
		}
		else
		{
			getManager(me)->getDefaultScript()->
				set(SLAPD_KEY_ADD_ORG_ENTRIES, "Yes");
			getManager(me)->getDefaultScript()->
				set(SLAPD_KEY_INSTALL_LDIF_FILE, buf);
		}
	}

	return DIALOG_NEXT;
}

DialogInput askOrgSize(
"Your directory will be populated with entries based on the size of\n"
"your organization.  The choices are small or large.  Please specify 1\n"
"for small and 2 for large.\n",

"Organization size (1 or 2)",

"1",

askOrgSizeSetup,
askOrgSizeNext
);

static DialogAction
askOrgSizeSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER))
		return action;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_ADD_ORG_ENTRIES))
		return action;
	else if (dialogSetup(me, SLAPD_KEY_ORG_SIZE, "1") &&
			 getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askOrgSizeNext(Dialog *me)
{
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER))
		return DIALOG_NEXT;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_ADD_ORG_ENTRIES))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;
   char testbuf[1024];
   int num, err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getDefaultScript()->set(SLAPD_KEY_ORG_SIZE, tmp);

   num = atoi(tmp);
   if (num != 1 && num != 2)
   {
	   sprintf(testbuf, "Please enter a 1 or a 2\n\n");
	   err = -1;
   }

   if (err)
   {
      DialogManagerType::showAlert(testbuf);
      return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogYesNo askReplication(
"Replication is used to duplicate all or part of a directory server to\n"
"another directory server.  This can be used for failsafe purposes, to\n"
"ensure that the directory data is always online and up-to-date in case\n"
"one server goes down.  It is also useful for distributing directory\n"
"data from a central main repository to remote directory servers.\n",

"Do you want to configure this directory server\nto use replication?",

"No",

askReplicationSetup,
askReplicationNext
);

static DialogAction
askReplicationSetup(Dialog *me)
{
	me = me;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askReplicationNext(Dialog *me)
{
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_USE_REPLICATION, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_REPLICATION))
	{
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SETUP_SUPPLIER, "No");
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SETUP_CONSUMER, "No");
	}

	return DIALOG_NEXT;
}

DialogYesNo askCIR(
"You may want to set up your directory server as a consumer server to\n"
"receive replicated entries from another directory server.  The first\n"
"two of the following methods configure this server as a consumer:\n\n"
"1) The supplier server will push its entries to this server (SIR)\n"
"2) This server will pull the entries from the supplier (CIR)\n"
"3) This server will not be a consumer for replication (NONE)\n",

"Do you want to set up this server as a consumer\n"
"for replication? (1, 2, or 3)",

"3",

askCIRSetup,
askCIRNext
);

static DialogAction
askCIRSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_REPLICATION))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_REPLICATION))
		return DIALOG_NEXT;

	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "3";
	}

	int val = atoi(buf);
	if (!val || val < 1 || val > 3)
	{
	   DialogManagerType::showAlert("Please enter a 1, 2, or 3.");
	   return DIALOG_SAME;
	}
	else if (val == 3)
	{
		buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SETUP_CONSUMER, buf);

	return DIALOG_NEXT;
}

DialogYesNo askSIR(
"You may want to set up your directory server as a supplier server to\n"
"replicate its entries to another directory server.  The first two of\n"
"the following methods configure this server as a supplier:\n\n"
"1) This server will push its entries to another one (SIR)\n"
"2) Another server will pull entries from this one (CIR)\n"
"3) This server will not be a supplier for replication (NONE)\n",

"Do you want to set up this server as a supplier\n"
"for replication? (1, 2, or 3)",

"3",

askSIRSetup,
askSIRNext
);

static DialogAction
askSIRSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_REPLICATION))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_REPLICATION))
		return DIALOG_NEXT;

	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "3";
	}

	int val = atoi(buf);
	if (!val || val < 1 || val > 3)
	{
	   DialogManagerType::showAlert("Please enter a 1, 2, or 3.");
	   return DIALOG_SAME;
	}
	else if (val == 3)
	{
		buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SETUP_SUPPLIER, buf);

	return DIALOG_NEXT;
}

DialogInput askCIRHost(
"Please specify the host name of the server from which the replicated\n"
"entries will be copied.\n",

"Supplier host name",

0,

askCIRHostSetup,
askCIRHostNext
);

static DialogAction
askCIRHostSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;
	else if (dialogSetup(me, SLAPD_KEY_CIR_HOST, 0) &&
			 getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRHostNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;
   int err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_HOST, tmp);

   if (!tmp || !isValid(tmp))
   {
	   DialogManagerType::showAlert("Please enter a valid hostname");
	   return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askCIRPort(
"Please specify the port of the server from which the replicated\n"
"entries will be copied.\n",

"Supplier port",

"389",

askCIRPortSetup,
askCIRPortNext
);

static DialogAction
askCIRPortSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	const char *defaultPort = "389";
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_CIR_SECURITY_ON))
		defaultPort = "636";

	if (dialogSetup(me, SLAPD_KEY_CIR_PORT, defaultPort) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRPortNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;

   const char *buf = me->input();
   const char *tmp;
   char testbuf[1024];
   int port, err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_PORT, tmp);

   port = atoi(tmp);
   sprintf(testbuf, "%d", port);
   if (strncmp(testbuf, tmp, 6) || port > MAXPORT || port < 1)
   {
      sprintf(testbuf, "OVERFLOW ERROR: Unable to bind to port %d\n"
                 "Please choose another port between 1 and %d.\n\n",
                   port, MAXPORT);
      err = -1;
   } 

   if (err)
   {
      DialogManagerType::showAlert(testbuf);
      return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askCIRDN(
"Replication requires that this consumer has access to the portion of\n"
"the remote directory to be replicated.  This requires a bind DN and\n"
"password for access to the supplier.  You will first be asked for the\n"
"bind DN, then the password.\n",

"Replication DN",

NULL,

askCIRDNSetup,
askCIRDNNext
);

static DialogAction
askCIRDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_CIR_BINDDN, getManager(me)->getConsumerDN()) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRDNNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;

	const char *slapdUser;
	char *slapdPwd = 0;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		slapdUser = me->defaultAns();
	}
	else
	{
		slapdUser = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_BINDDN, slapdUser);

	// check to see if it is a valid DN
	if (!isAValidDN(slapdUser))
	{
	 	DialogManagerType::showAlert("The consumer must be a valid DN.");
	 	return DIALOG_SAME;
	}
	else if (!isValid(slapdUser))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				free(inp);
				continue;
			}
			else
			{
				slapdPwd = inp;
				break;
			}
		}
	}

	if (slapdPwd)
	{
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_BINDDNPWD, slapdPwd);
		free(slapdPwd);
	}

	return DIALOG_NEXT;
}

DialogInput askCIRSuffix(
"Please enter the full DN of the part of the tree to replicate,\n"
"including the suffix (e.g. ou=People, o=company.com).\n",

"Enter the directory path",

NULL,

askCIRSuffixSetup,
askCIRSuffixNext
);

static DialogAction
askCIRSuffixSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;
	
	if (dialogSetup(me, SLAPD_KEY_CIR_SUFFIX, getManager(me)->getDefaultSuffix()) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRSuffixNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;
	
	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_SUFFIX, val);

	// check val to see if it is a valid DN
	if (!isAValidDN(val))
	{
	 	DialogManagerType::showAlert("The suffix must be a valid DN.");
	 	return DIALOG_SAME;
	}
	if (!isValid(val))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	int status;
	if (status = getManager(me)->verifyRemoteLdap(
								  SLAPD_KEY_CIR_HOST,
								  SLAPD_KEY_CIR_PORT,
								  SLAPD_KEY_CIR_SUFFIX,
								  SLAPD_KEY_CIR_BINDDN,
								  SLAPD_KEY_CIR_BINDDNPWD
		                         )
	   )
	{
		ostrstream msg;
		msg << "Could not connect to ldap://"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_CIR_HOST)
			<< ":"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_CIR_PORT)
			<< "/"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_CIR_SUFFIX)
			<< endl << "for bind DN "
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_CIR_BINDDN)
			<< " status = " << status << endl
			<< "Please check your typing.  If you have mis-typed, you can backup"
			<< endl
			<< "and retype.  Otherwise, the remote server may be down at this time."
			<< endl
			<< "The replication agreement will be created anyway.  Proceeding..."
			<< endl << ends;
		DialogManagerType::showAlert(msg.str());
		delete [] msg.str();
		return DIALOG_NEXT;
	}

	return DIALOG_NEXT;
}

DialogYesNo askCIRSSL(
"You may use SSL authentication for replication if you have enabled it\n"
"on the remote server.\n",

"Do you want to use SSL?",

"No",

askCIRSSLSetup,
askCIRSSLNext
);

static DialogAction
askCIRSSLSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRSSLNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;
	
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_SECURITY_ON, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

	return DIALOG_NEXT;
}

DialogInput askCIRInterval(
"Please specify the time interval to check the remote server for new\n"
"entries to be replicated.  Use the directory server console to set up\n"
"more fine-grained control.  Specify the time in minutes.  Use a 0\n"
"(zero) to indicate that changes should be propagated immediately all\n"
"the time.\n",

"Replication Sync Interval (in minutes)",

"10",

askCIRIntervalSetup,
askCIRIntervalNext
);

static DialogAction
askCIRIntervalSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_CIR_INTERVAL, "10") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRIntervalNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;
	
	const char *buf = me->input();
	const char *tmp;
	char testbuf[1024];
	int interval, err = 0;
	
	if (buf[0] == 0)
	{
		tmp = me->defaultAns();
	}
	else
	{
		tmp = buf;
	}
	
	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_INTERVAL, tmp);
	
	interval = atoi(tmp);
	if (!isdigit((*tmp)) || interval < 0)
	{
		sprintf(testbuf, "Please specify an integer greater than or equal to 0");
		err = -1;
	} 
	
	if (err)
	{
		DialogManagerType::showAlert(testbuf);
		return DIALOG_SAME;
	}
	
	return DIALOG_NEXT;
}

DialogInput askChangeLogSuffix(
"Changes to the database will be kept under a separate suffix in the\n"
"directory tree.  These changes are used to replicate changes to other\n"
"directory servers.\n",

"Changelog suffix",

"cn=changelog",

askChangeLogSuffixSetup,
askChangeLogSuffixNext
);

static DialogAction
askChangeLogSuffixSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER))
		return action;
	
	if (dialogSetup(me, SLAPD_KEY_CHANGELOGSUFFIX, "cn=changelog") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askChangeLogSuffixNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER))
		return DIALOG_NEXT;
	
	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CHANGELOGSUFFIX, val);

	// check to see if val is a valid DN
	if (!isAValidDN(val))
	{
	 	DialogManagerType::showAlert("The ChangeLog suffix must be a valid DN");
	 	return DIALOG_SAME;
	}
	else if (!isValid(val))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askChangeLogDir(
"Changes to the main database will be kept in a separate database\n"
"stored in a separate directory path, usually under your server\n"
"instance directory.\n",

"Changelog database\n"
"directory",

NULL,

askChangeLogDirSetup,
askChangeLogDirNext
);

static DialogAction
askChangeLogDirSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER))
		return action;
	
	NSString dir = NSString(
		getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ROOT)
		) + "/slapd-" +
		getManager(me)->getDefaultScript()->get(SLAPD_KEY_SERVER_IDENTIFIER) +
		"/logs/changelogdb";
	if (dialogSetup(me, SLAPD_KEY_CHANGELOGDIR, dir) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askChangeLogDirNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER))
		return DIALOG_NEXT;
	
	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CHANGELOGDIR, val);

	if (InstUtil::dirExists(val) && !InstUtil::dirWritable(val))
	{
		DialogManagerType::showAlert("You do not have access to that directory.  Please try again.");
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askReplicationDN(
"In order to allow remote servers to replicate new entries to this\n"
"server, the remote server must have the ability to bind to this server\n"
"as some entity with permission to do so.  The Supplier DN is the DN of\n"
"the entity the remote server will use to connect to this server to\n"
"supply updates.  The Supplier DN also requires a password which you\n"
"will be prompted for after the DN.\n",

"Supplier Bind DN",

"cn=supplier",

askReplicationDNSetup,
askReplicationDNNext
);

static DialogAction
askReplicationDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "1"))
		return action;
	
	if (dialogSetup(me, SLAPD_KEY_REPLICATIONDN, "cn=supplier") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askReplicationDNNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "1"))
		return DIALOG_NEXT;
	
	const char *replicationdn;
	const char *replicationpw;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		replicationdn = me->defaultAns();
	}
	else
	{
		replicationdn = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_REPLICATIONDN, replicationdn);

	// check to see if it is a valid DN
	if (!isAValidDN(replicationdn))
	{
	 	DialogManagerType::showAlert("The Supplier Bind DN must be a valid DN");
	 	return DIALOG_SAME;
	}
	if (!isValid(replicationdn))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				continue;
			}
			else if (contains8BitChars(inp))
			{
				DialogManagerType::showAlert("Password must contain 7 bit characters only.");
				return DIALOG_SAME;
			}
			else if (!isValid(inp))
			{
				DialogManagerType::showAlert("Please enter a valid password.");
				return DIALOG_SAME;
			}
			else
			{
				me->showString("Password (again): ");
				if (me->getPassword() == 0)
				{
					return DIALOG_PREV;
				}
				else
				{
					replicationpw = me->input();
					if (strcmp(inp,replicationpw))
					{
						DialogManagerType::showAlert("Passwords don't match.");
						return DIALOG_SAME;
					}
					break;
				}
			}
			free(inp);
		}
	}
	getManager(me)->getDefaultScript()->set(SLAPD_KEY_REPLICATIONPWD, replicationpw);
	return DIALOG_NEXT;
}

DialogInput askConsumerDN(
"In order to allow remote servers to replicate new entries from this\n"
"server, the remote server must have the ability to bind to this server\n"
"as some entity with permission to do so.  The Consumer DN is the DN of\n"
"the entity the remote server will use to connect to this server to\n"
"pull the new entries.  This entity will have access to the entire\n"
"database as well as the changelog entries.  The Consumer DN also\n"
"requires a password which you will be prompted for after the DN.  If\n"
"you leave this entry blank, no consumer bind DN will be created.  The\n"
"default is no consumer bind DN.\n",

"Consumer Bind DN",

NULL,

askConsumerDNSetup,
askConsumerDNNext
);

static DialogAction
askConsumerDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "2"))
		return action;
	
	if (dialogSetup(me, SLAPD_KEY_CONSUMERDN, 0) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askConsumerDNNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "2"))
		return DIALOG_NEXT;
	
	const char *consumerdn;
	const char *consumerpw;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		consumerdn = me->defaultAns();
	}
	else
	{
		consumerdn = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CONSUMERDN, consumerdn);

	if (!consumerdn || !*consumerdn ||
		!strncasecmp(consumerdn, "none", strlen(consumerdn)))
	{
		getManager(me)->getDefaultScript()->remove(SLAPD_KEY_CONSUMERDN);
		return DIALOG_NEXT;
	}

	// check to see if it is a valid dn
	if (!isAValidDN(consumerdn))
	{
	 	DialogManagerType::showAlert("The Consumer Bind DN must be a valid DN");
	 	return DIALOG_SAME;
	}
	else if (!isValid(consumerdn))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				continue;
			}
			else if (contains8BitChars(inp))
			{
				DialogManagerType::showAlert("Password must contain 7 bit characters only.");
				return DIALOG_SAME;
			}
			else if (!isValid(inp))
			{
				DialogManagerType::showAlert("Please enter a valid password.");
				return DIALOG_SAME;
			}
			else
			{
				me->showString("Password (again): ");
				if (me->getPassword() == 0)
				{
					return DIALOG_PREV;
				}
				else
				{
					consumerpw = me->input();
					if (strcmp(inp,consumerpw))
					{
						DialogManagerType::showAlert("Passwords don't match.");
						return DIALOG_SAME;
					}
					break;
				}
			}
			free(inp);
		}
	}
	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CONSUMERPWD, consumerpw);
	return DIALOG_NEXT;
}

DialogInput askSIRHost(
"Please specify the host name of the server to which the replicated\n"
"entries will be pushed.\n",

"Consumer host name",

0,

askSIRHostSetup,
askSIRHostNext
);

static DialogAction
askSIRHostSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;
	else if (dialogSetup(me, SLAPD_KEY_SIR_HOST, 0) &&
			 getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRHostNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;
   int err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_HOST, tmp);

   if (!tmp || !isValid(tmp))
   {
	   DialogManagerType::showAlert("Please enter a valid hostname");
	   return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askSIRPort(
"Please specify the port of the server to which the replicated entries\n"
"will be pushed.\n",

"Consumer port",

"389",

askSIRPortSetup,
askSIRPortNext
);

static DialogAction
askSIRPortSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	const char *defaultPort = "389";
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_SIR_SECURITY_ON))
		defaultPort = "636";

	if (dialogSetup(me, SLAPD_KEY_SIR_PORT, defaultPort) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRPortNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;

   const char *buf = me->input();
   const char *tmp;
   char testbuf[1024];
   int port, err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_PORT, tmp);

   port = atoi(tmp);
   sprintf(testbuf, "%d", port);
   if (strncmp(testbuf, tmp, 6) || port > MAXPORT || port < 1)
   {
      sprintf(testbuf, "OVERFLOW ERROR: Unable to bind to port %d\n"
                 "Please choose another port between 1 and %d.\n\n",
                   port, MAXPORT);
      err = -1;
   } 

   if (err)
   {
      DialogManagerType::showAlert(testbuf);
      return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askSIRDN(
"Replication requires that this supplier has access to the portion of\n"
"the remote directory to be replicated.  This requires a bind DN and\n"
"password for access to the consumer.  You will first be asked for the\n"
"bind DN, then the password.  This is the same as the Supplier DN on\n"
"the consumer.\n",

"Replication DN on the Consumer",

"cn=supplier",

askSIRDNSetup,
askSIRDNNext
);

static DialogAction
askSIRDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_SIR_BINDDN, "cn=supplier") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRDNNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;

	const char *slapdUser;
	char *slapdPwd = 0;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		slapdUser = me->defaultAns();
	}
	else
	{
		slapdUser = buf;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_BINDDN, slapdUser);

	// check to see if it is a valid dn
	if (!isAValidDN(slapdUser))
	{
	 	DialogManagerType::showAlert("The Consumer Replication DN must be a valid DN");
	 	return DIALOG_SAME;
	}
	else if (!isValid(slapdUser))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				free(inp);
				continue;
			}
			else
			{
				slapdPwd = inp;
				break;
			}
		}
	}

	if (slapdPwd)
	{
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_BINDDNPWD, slapdPwd);
		free(slapdPwd);
	}

	return DIALOG_NEXT;
}

DialogInput askSIRSuffix(
"Please enter the full DN of the part of the tree to replicate,\n"
"including the suffix (e.g. ou=People, o=company.com).\n",

"Directory path (DN)",

NULL,

askSIRSuffixSetup,
askSIRSuffixNext
);

static DialogAction
askSIRSuffixSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_SIR_SUFFIX, getManager(me)->getDefaultSuffix()) &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRSuffixNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_SUFFIX, val);

	// check to see if it is a valid dn
	if (!isAValidDN(val))
	{
	 	DialogManagerType::showAlert("The suffix must be a valid DN");
	 	return DIALOG_SAME;
	}
	else if (!isValid(val))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	int status;
	if (status = getManager(me)->verifyRemoteLdap(
								  SLAPD_KEY_SIR_HOST,
								  SLAPD_KEY_SIR_PORT,
								  SLAPD_KEY_SIR_SUFFIX,
								  SLAPD_KEY_SIR_BINDDN,
								  SLAPD_KEY_SIR_BINDDNPWD
								 )
	   )
	{
		ostrstream msg;
		msg << "Could not connect to ldap://"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_HOST)
			<< ":"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_PORT)
			<< "/"
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_SUFFIX)
			<< endl << "for bind DN "
			<< getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_BINDDN)
			<< " status = " << status << endl
			<< "Please check your typing.  If you have mis-typed, you can backup"
			<< endl
			<< "and retype.  Otherwise, the remote server may be down at this time."
			<< endl
			<< "The replication agreement will be created anyway.  Proceeding..."
			<< endl << ends;
		DialogManagerType::showAlert(msg.str());
		delete [] msg.str();
		return DIALOG_NEXT;
	}

	return DIALOG_NEXT;
}

DialogYesNo askSIRSSL(
"You may use SSL authentication for replication if you have enabled it\n"
"on the remote server.\n",

"Do you want to use SSL?",

"No",

askSIRSSLSetup,
askSIRSSLNext
);

static DialogAction
askSIRSSLSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRSSLNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;
	
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_SECURITY_ON, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

	return DIALOG_NEXT;
}

DialogInput askCIRDays(
"Please enter the days of the week on which you would like replication\n"
"to occur.  The days are specified by a number.  For example, use 0 for\n"
"Sunday, 1 for Monday, etc.  Use 6 for Saturday.  You may not specify a\n"
"number greater than 6 or less than 0.  The numbers should be entered\n"
"one after another in a list.  For example, 0123 would be Sunday,\n"
"Monday, Tuesday, and Wednesday.  06 would be Sunday and Saturday.  The\n"
"default is everyday.\n",

"Enter the replication days",

"all",

askCIRDaysSetup,
askCIRDaysNext
);

static DialogAction
askCIRDaysSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_CIR_DAYS, "all") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	const char *tmp;
	if ((tmp = getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_DAYS)) &&
		!*tmp)
		me->setDefaultAns("all");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRDaysNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_DAYS, val);

	int status = 0;
	ostrstream msg;

	char realval[8] = "-------";
	if (!strncasecmp(val, "all", strlen(val)))
		strcpy(realval, ""); // default is everyday
	else
	{
		for (const char *ptr = val; *ptr; ++ptr)
		{
			if (!isdigit(*ptr))
			{
				msg << "The string [" << val << "] contains non-digit characters."
					<< "  Please re enter the string." << ends;
				status = 1;
				break;
			}

			int ival = (int)(*ptr) - (int)'0';
			if (ival > 6)
			{
				msg << "The string contains an invalid value [" << ival << "]."
					<< "  Please re enter the string." << ends;
				status = 2;
				break;
			}

			// this step makes sure we get the numbers in order with no duplicates
			realval[ival] = *ptr;
		}

		if (status)
		{
			DialogManagerType::showAlert(msg.str());
			delete [] msg.str();
			return DIALOG_SAME;
		}

		// realval now contains a string like
		// 0---4-6, but we really want 046
		int index = 0;
		for (char *p2 = realval; *p2; ++p2)
		{
			if (*p2 != '-')
				realval[index++] = *p2;
		}
		realval[index] = 0;

	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_DAYS, realval);

	return DIALOG_NEXT;
}

DialogInput askSIRDays(
"Please enter the days of the week on which you would like replication\n"
"to occur.  The days are specified by a number.  For example, use 0 for\n"
"Sunday, 1 for Monday, etc.  Use 6 for Saturday.  You may not specify a\n"
"number greater than 6 or less than 0.  The numbers should be entered\n"
"one after another in a list.  For example, 0123 would be Sunday,\n"
"Monday, Tuesday, and Wednesday.  06 would be Sunday and Saturday.  The\n"
"default is everyday.\n",

"Enter the replication days",

"all",

askSIRDaysSetup,
askSIRDaysNext
);

static DialogAction
askSIRDaysSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_SIR_DAYS, "all") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	const char *tmp;
	if ((tmp = getManager(me)->getDefaultScript()->get(SLAPD_KEY_SIR_DAYS)) &&
		!*tmp)
		me->setDefaultAns("all");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRDaysNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_DAYS, val);

	int status = 0;
	ostrstream msg;

	char realval[8] = "-------";
	if (!strncasecmp(val, "all", strlen(val)))
		strcpy(realval, ""); // default is everyday
	else
	{
		for (const char *ptr = val; *ptr; ++ptr)
		{
			if (!isdigit(*ptr))
			{
				msg << "The string [" << val << "] contains non-digit characters."
					<< "  Please re enter the string." << ends;
				status = 1;
				break;
			}

			int ival = (int)(*ptr) - (int)'0';
			if (ival > 6)
			{
				msg << "The string contains an invalid value [" << ival << "]."
					<< "  Please re enter the string." << ends;
				status = 2;
				break;
			}

			// this step makes sure we get the numbers in order with no duplicates
			realval[ival] = *ptr;
		}

		if (status)
		{
			DialogManagerType::showAlert(msg.str());
			delete [] msg.str();
			return DIALOG_SAME;
		}

		// realval now contains a string like
		// 0---4-6, but we really want 046
		int index = 0;
		for (char *p2 = realval; *p2; ++p2)
		{
			if (*p2 != '-')
				realval[index++] = *p2;
		}
		realval[index] = 0;
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_DAYS, realval);

	return DIALOG_NEXT;
}

DialogInput askCIRTimes(
"Please enter the time of day you would like replication to occur.  The\n"
"time is specified as a range in the form HHMM-HHMM in 24 hour time.\n"
"HH represents the hour portion of the time, and MM the minutes.\n"
"Numbers less than 10 should be preceeded by a 0.  For example, to\n"
"enable replication between 1 am and 4:30 am, specify 0100-0430.  To\n"
"specify 11 am to 9 pm, use 1100-2100.  12 am to 12:59 am is specified\n"
"as 0000-0059.  The default is all day.\n",

"Enter the replication times",

"all day",

askCIRTimesSetup,
askCIRTimesNext
);

static DialogAction
askCIRTimesSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_CIR_TIMES, "all day") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askCIRTimesNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_CONSUMER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_CONSUMER), "2"))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	if (!strncasecmp(val, "all day", strlen(val)))
	{
		val = "";
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_CIR_TIMES, val);
		return DIALOG_NEXT;
	}

	int status = 0;
	ostrstream msg;

	int pos = 0;
	// format should be HHMM-HHMM
	int maxvals[9] = {0, 23, 0, 59, 0, 0, 23, 0, 59};
	char teststr[3]; // 2 digits plus \0
	int testindex = 0;
	for (const char *ptr = val; *ptr; ++ptr, ++pos)
	{
		// position 4 should contain the '-'
		if (pos == 4 && *ptr != '-')
		{
			msg << "The time specification [" << val << "] is invalid.\n"
				<< "Please re enter the string." << ends;
			status = 1;
			break;
		}
		else if (pos == 4)
			continue;

		if (!isdigit(*ptr) && pos != 4)
		{
			msg << "The time specification [" << val << "] contains non-digit characters.\n"
				<< "Please re enter the string." << ends;
			status = 2;
			break;
		}

		teststr[testindex++] = *ptr;
		if (pos == 1 || pos == 3 || pos == 6 || pos == 8)
		{
			teststr[testindex] = 0;
			testindex = 0;
			if (teststr[0] == '0')
				teststr[0] = ' ';
			int ival = atoi(teststr);
			if (ival > maxvals[pos])
			{
				msg << "The string contains an invalid value [" << ival << "].\n"
					<< "Please re enter the string." << ends;
				status = 3;
				break;
			}
		}
	}

	if (pos != 9)
	{
		msg << "The string [" << val << "] is invalid.\n"
			<< "Please re enter the string." << ends;
		status = 4;
	}

	if (status)
	{
		DialogManagerType::showAlert(msg.str());
		delete [] msg.str();
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askSIRTimes(
"Please enter the time of day you would like replication to occur.  The\n"
"time is specified as a range in the form HHMM-HHMM in 24 hour time.\n"
"HH represents the hour portion of the time, and MM the minutes.\n"
"Numbers less than 10 should be preceeded by a 0.  For example, to\n"
"enable replication between 1 am and 4:30 am, specify 0100-0430.  To\n"
"specify 11 am to 9 pm, use 1100-2100.  12 am to 12:59 am is specified\n"
"as 0000-0059.  The default is all day.\n",

"Enter the replication times",

"all day",

askSIRTimesSetup,
askSIRTimesNext
);

static DialogAction
askSIRTimesSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return action;

	if (dialogSetup(me, SLAPD_KEY_SIR_TIMES, "all day") &&
		getManager(me)->installMode() == Silent)
		return DIALOG_ERROR;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askSIRTimesNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_SETUP_SUPPLIER) ||
		strcmp(getManager(me)->getDefaultScript()->get(SLAPD_KEY_SETUP_SUPPLIER), "1"))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	if (!strncasecmp(val, "all day", strlen(val)))
	{
		val = "";
		getManager(me)->getDefaultScript()->set(SLAPD_KEY_SIR_TIMES, val);
		return DIALOG_NEXT;
	}

	int status = 0;
	ostrstream msg;

	int pos = 0;
	// format should be HHMM-HHMM
	int maxvals[9] = {0, 23, 0, 59, 0, 0, 23, 0, 59};
	char teststr[3]; // 2 digits plus \0
	int testindex = 0;
	for (const char *ptr = val; *ptr; ++ptr, ++pos)
	{
		// position 4 should contain the '-'
		if (pos == 4 && *ptr != '-')
		{
			msg << "The time specification [" << val << "] is invalid.\n"
				<< "Please re enter the string." << ends;
			status = 1;
			break;
		}
		else if (pos == 4)
			continue;

		if (!isdigit(*ptr) && pos != 4)
		{
			msg << "The time specification [" << val << "] contains non-digit characters.\n"
				<< "Please re enter the string." << ends;
			status = 2;
			break;
		}

		teststr[testindex++] = *ptr;
		if (pos == 1 || pos == 3 || pos == 6 || pos == 8)
		{
			teststr[testindex] = 0;
			testindex = 0;
			if (teststr[0] == '0')
				teststr[0] = ' ';
			int ival = atoi(teststr);
			if (ival > maxvals[pos])
			{
				msg << "The string contains an invalid value [" << ival << "].\n"
					<< "Please re enter the string." << ends;
				status = 3;
				break;
			}
		}
	}

	if (pos != 9)
	{
		msg << "The string [" << val << "] is invalid.\n"
			<< "Please re enter the string." << ends;
		status = 4;
	}

	if (status)
	{
		DialogManagerType::showAlert(msg.str());
		delete [] msg.str();
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogYesNo askUseExistingMC(
"Brandx server information is stored in the Brandx configuration\n"
"directory server, which you may have already set up.  If so, you\n"
"should configure this server to be managed by the configuration\n"
"server.  To do so, the following information about the configuration\n"
"server is required: the fully qualified host name of the form\n"
"<hostname>.<domainname>(e.g. hostname.domain.com), the port number,\n"
"the suffix, and the DN and password of a user having permission to\n"
"write the configuration information, usually the Brandx\n"
"configuration directory administrator.\n\n"
"If you want to install this software as a standalone server, or if you\n"
"want this instance to serve as your Brandx configuration directory\n"
"server, press Enter.\n",

"Do you want to register this software with an existing\n"
"Brandx configuration directory server?",

"No",

askUseExistingMCSetup,
askUseExistingMCNext
);

static DialogAction
askUseExistingMCSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askUseExistingMCSetup" << endl;
#endif
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;

	if (action != DIALOG_PREV)
	{
		if (getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_URL))
		{
			// tell the instance creator not to create the Config entries
			// new instance
			getManager(me)->getDefaultScript()->set(
				SLAPD_KEY_USE_EXISTING_MC, "Yes");
			getManager(me)->getDefaultScript()->set(
				SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "No");
		}
		else
		{
			getManager(me)->getDefaultScript()->set(
				SLAPD_KEY_USE_EXISTING_MC, "No");
			getManager(me)->getDefaultScript()->set(
				SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "Yes");
		}
	}

	dialogSetup(me, SLAPD_KEY_USE_EXISTING_MC, "No");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askUseExistingMCSetup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

#if DEBUG > 1
	cerr << "Leaving askUseExistingMCSetup DIALOG_SAME" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askUseExistingMCNext(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askUseExistingMCNext" << endl;
#endif
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_USE_EXISTING_MC, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

#if DEBUG > 1
	cerr << "Leaving askUseExistingMCNext" << endl;
#endif
	return DIALOG_NEXT;
}

DialogInput askMCHost(
"Enter the fully qualified domain name of the Brandx configuration\n"
"directory server host in the form <hostname>.<domainname>\n"
"(e.g. hostname.domain.com).\n",

"Brandx configuration directory server\nhost name",

0,

askMCHostSetup,
askMCHostNext
);

static DialogAction
askMCHostSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return action;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askMCHostNext(Dialog *me)
{
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getBaseScript()->set(SLAPD_KEY_K_LDAP_HOST, tmp);

   if (!tmp || !isValid(tmp))
   {
	   DialogManagerType::showAlert("Please enter a valid hostname");
	   return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askMCPort(
"Please specify the port number on which the Brandx configuration\n"
"directory server listens.\n",

"Brandx configuration directory server\nport number",

"389",

askMCPortSetup,
askMCPortNext
);

static DialogAction
askMCPortSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return action;
	
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askMCPortNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;
   char testbuf[1024];
   int port, err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getBaseScript()->set(SLAPD_KEY_K_LDAP_PORT, tmp);

   port = atoi(tmp);
   sprintf(testbuf, "%d", port);
   if (strncmp(testbuf, tmp, 6) || port > MAXPORT || port < 1)
   {
      sprintf(testbuf, "OVERFLOW ERROR: Unable to bind to port %d\n"
                 "Please choose another port between 1 and %d.\n\n",
                   port, MAXPORT);
      err = -1;
   } 

   if (err)
   {
      DialogManagerType::showAlert(testbuf);
      return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askMCDN(
"To write configuration information into the Brandx configuration\n"
"directory, you must bind to the server as an entity with the\n"
"appropriate permissions.  Usually, the Brandx configuration\n"
"directory administrator is used for this purpose, although you can\n"
"give other directory accounts the proper access.\n",

"Brandx configuration directory server\nadministrator ID",

0,

askMCDNSetup,
askMCDNNext
);

static DialogAction
askMCDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return action;

	dialogSetup(me, SLAPD_KEY_SERVER_ADMIN_ID, "admin");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askMCDNNext(Dialog *me)
{
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;

	const char *slapdUser;
	char *slapdPwd = 0;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		slapdUser = me->defaultAns();
	}
	else
	{
		slapdUser = buf;
	}

	getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_ID, slapdUser);

	if (!isValid(slapdUser))
	{
		DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				free(inp);
				continue;
			}
			else
			{
				slapdPwd = inp;
				break;
			}
		}
	}

	if (slapdPwd)
	{
		getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_PWD, slapdPwd);
		free(slapdPwd);

		int status;
		if (status = getManager(me)->verifyRemoteLdap(
									  SLAPD_KEY_K_LDAP_HOST,
									  SLAPD_KEY_K_LDAP_PORT,
									  SLAPD_KEY_BASE_SUFFIX,
									  SLAPD_KEY_SERVER_ADMIN_ID,
									  SLAPD_KEY_SERVER_ADMIN_PWD
			                         )
		   )
		{
			ostrstream msg;
			msg << "Could not connect to ldap://"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_HOST)
				<< ":"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_PORT)
				<< "/"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_BASE_SUFFIX)
				<< endl << "for bind DN "
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID)
				<< " status = " << status << endl
				<< "Please check your typing.  If you have mis-typed, you can backup"
				<< endl
				<< "and retype.  Otherwise, the remote server may be down at this time."
				<< endl
				<< "The installation cannot proceed."
				<< endl << ends;
			DialogManagerType::showAlert(msg.str());
			delete [] msg.str();
			return DIALOG_SAME;
		}
		else if ((getManager(me)->installType() < Custom) &&
				 (status = getManager(me)->verifyAdminDomain(
									  SLAPD_KEY_K_LDAP_HOST,
									  SLAPD_KEY_K_LDAP_PORT,
									  SLAPD_KEY_BASE_SUFFIX,
									  SLAPD_KEY_ADMIN_DOMAIN,
									  SLAPD_KEY_SERVER_ADMIN_ID,
									  SLAPD_KEY_SERVER_ADMIN_PWD
					 ))
			)
		{
			ostrstream msg;
			msg << "Could not find the Admin Domain "
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_ADMIN_DOMAIN)
				<< " in the server" << endl << "ldap://"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_HOST)
				<< ":"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_PORT)
				<< "/"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_BASE_SUFFIX)
				<< endl << "for bind DN "
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID)
				<< " status = " << status << endl
				<< "You may need to re-run setup in Custom mode in order to specify"
				<< endl
				<< "the correct Admin Domain."
				<< endl
				<< "The installation cannot proceed."
				<< endl << ends;
			DialogManagerType::showAlert(msg.str());
			delete [] msg.str();
			return DIALOG_SAME;
		}
	}

	// tell the instance creator not to create the config entries in the
	// new instance
	getManager(me)->getDefaultScript()->set(SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "No");

	return DIALOG_NEXT;
}

DialogYesNo askDisableSchemaChecking(
"If you are going to import an old database immediately after or during\n"
"installation, and you think you may have problems with your old\n"
"schema, you may want to turn off schema checking until after the\n"
"import.  If you choose to do this, schema checking will remain off\n"
"until you manually turn it back on.  Brandx recommends that you turn\n"
"it back on as soon as possible.\n",

"Do you want to disable schema checking?",

"No",

askDisableSchemaCheckingSetup,
askDisableSchemaCheckingNext
);

static DialogAction
askDisableSchemaCheckingSetup(Dialog *me)
{
	me = me;

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askDisableSchemaCheckingNext(Dialog *me)
{
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_DISABLE_SCHEMA_CHECKING, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

	return DIALOG_NEXT;
}

DialogInput askMCAdminDomain(
"The Administration Domain is a part of the configuration directory\n"
"server used to store information about Brandx software.  If you are\n"
"managing multiple software releases at the same time, or managing\n"
"information about multiple domains, you may use the Administration\n"
"Domain to keep them separate.\n\n"
"If you are not using administrative domains, press Enter to select the\n"
"default.  Otherwise, enter some descriptive, unique name for the\n"
"administration domain, such as the name of the organization responsible\n"
"for managing the domain.\n",

"Administration Domain",

NULL,

askMCAdminDomainSetup,
askMCAdminDomainNext
);

static DialogAction
askMCAdminDomainSetup(Dialog *me)
{
	if (!getManager(me)->getBaseScript()->get(SLAPD_KEY_ADMIN_DOMAIN)) {
		getManager(me)->getBaseScript()->set(
			SLAPD_KEY_ADMIN_DOMAIN, getManager(me)->getDNSDomain());
	}

	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
	{
#if DEBUG > 1
		cerr << "leaving askMCAdminDomainSetup " << action << endl;
#endif
		return action;
	}

	// if we are creating the Configuration server, the admin domain will not
	// yet exist, and we need to ask the user to create one.  Otherwise, we are
	// installing into an existing one

	dialogSetup(me, SLAPD_KEY_ADMIN_DOMAIN, getManager(me)->getDNSDomain());

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askMCAdminDomainNext(Dialog *me)
{
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;

	const char *buf;
	int status = 0;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getBaseScript()->set(SLAPD_KEY_ADMIN_DOMAIN, val);

	if (!isValid(val))
	{
		DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}
	else if (isAValidDN(val))
	{
		DialogManagerType::showAlert("A DN is not allowed here.  Please enter a valid string.");
		return DIALOG_SAME;
	}
	else if (status = getManager(me)->verifyAdminDomain(
									  SLAPD_KEY_K_LDAP_HOST,
									  SLAPD_KEY_K_LDAP_PORT,
									  SLAPD_KEY_BASE_SUFFIX,
									  SLAPD_KEY_ADMIN_DOMAIN,
									  SLAPD_KEY_SERVER_ADMIN_ID,
									  SLAPD_KEY_SERVER_ADMIN_PWD
			                         )
		)
	{
		ostrstream msg;
		msg << "Could not find the Admin Domain "
			<< getManager(me)->getBaseScript()->get(SLAPD_KEY_ADMIN_DOMAIN)
			<< " in the server" << endl << "ldap://"
			<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_HOST)
			<< ":"
			<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_PORT)
			<< "/"
			<< getManager(me)->getBaseScript()->get(SLAPD_KEY_BASE_SUFFIX)
			<< endl << "for bind DN "
			<< getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID)
			<< " status = " << status << endl
			<< "Please check your typing.  If you have mis-typed, you can backup"
			<< endl
			<< "and retype.  Otherwise, the remote server may be down at this time."
			<< endl
			<< "The installation cannot proceed."
			<< endl << ends;
		DialogManagerType::showAlert(msg.str());
		delete [] msg.str();
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askAdminDomain(
"The Administration Domain is a part of the configuration directory\n"
"server used to store information about Brandx software.  If you are\n"
"managing multiple software releases at the same time, or managing\n"
"information about multiple domains, you may use the Administration\n"
"Domain to keep them separate.\n\n"
"If you are not using administrative domains, press Enter to select the\n"
"default.  Otherwise, enter some descriptive, unique name for the\n"
"administration domain, such as the name of the organization responsible\n"
"for managing the domain.\n",

"Administration Domain",

NULL,

askAdminDomainSetup,
askAdminDomainNext
);

static DialogAction
askAdminDomainSetup(Dialog *me)
{
	if (!getManager(me)->getBaseScript()->get(SLAPD_KEY_ADMIN_DOMAIN)) {
		getManager(me)->getBaseScript()->set(
			SLAPD_KEY_ADMIN_DOMAIN, getManager(me)->getDNSDomain());
	}

	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return action;

	// if we are creating the Configuration server, the admin domain will not
	// yet exist, and we need to ask the user to create one.  Otherwise, we are
	// installing into an existing one

	dialogSetup(me, SLAPD_KEY_ADMIN_DOMAIN, getManager(me)->getDNSDomain());

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askAdminDomainNext(Dialog *me)
{
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getBaseScript()->set(SLAPD_KEY_ADMIN_DOMAIN, val);

	if (!isValid(val))
	{
		DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}

	if (isAValidDN(val))
	{
		DialogManagerType::showAlert("A DN is not allowed here.  Please enter a valid string.");
		return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogYesNo askUseExistingUG(
"If you already have a directory server you want to use to store your\n"
"data, such as user and group information, answer Yes to the following\n"
"question.  You will be prompted for the host, port, suffix, and bind\n"
"DN to use for that directory server.\n\n"
"If you want this directory server to store your data, answer No.\n",

"Do you want to use another directory to store your data?",

"No",

askUseExistingUGSetup,
askUseExistingUGNext
);

static DialogAction
askUseExistingUGSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askUseExistingUGSetup" << endl;
#endif
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	// if this server is not an MC host, it must be a UG host
	if (getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
	{
		getManager(me)->getDefaultScript()->set(
			SLAPD_KEY_USE_EXISTING_UG, "No");
#if DEBUG > 1
		cerr << "Leaving askUseExistingUGSetup DIALOG_NEXT" << endl;
#endif
		return action;
	}
	else if (getManager(me)->getBaseScript()->get(SLAPD_KEY_USER_GROUP_LDAP_URL))
	{
		getManager(me)->getDefaultScript()->set(
			SLAPD_KEY_USE_EXISTING_UG, "Yes");
	}
	else
	{
		getManager(me)->getDefaultScript()->set(
			SLAPD_KEY_USE_EXISTING_UG, "No");
	}

	dialogSetup(me, SLAPD_KEY_USE_EXISTING_UG, "No");

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
	{
#if DEBUG > 1
		cerr << "Leaving askUseExistingUGSetup DIALOG_NEXT" << endl;
#endif
		return DIALOG_NEXT;
	}

#if DEBUG > 1
	cerr << "Leaving askUseExistingUGSetup DIALOG_SAME" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askUseExistingUGNext(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askUseExistingUGNext" << endl;
#endif
	const char *buf = me->input();
	if (!buf || !*buf)
	{
		buf = me->defaultAns();
		if (!buf || !*buf)
			buf = "No";
	}

	getManager(me)->getDefaultScript()->set(SLAPD_KEY_USE_EXISTING_UG, buf);

	if (!isValidYesNo(buf))
		return DIALOG_SAME;

#if DEBUG > 1
	cerr << "Leaving askUseExistingUGNext DIALOG_NEXT" << endl;
#endif
	return DIALOG_NEXT;
}

DialogInput askUGHost(
"Enter the fully qualified domain name of the user directory host of\n"
"the form <hostname>.<domainname> (e.g. hostname.domain.com).\n",

"User directory host name",

0,

askUGHostSetup,
askUGHostNext
);

static DialogAction
askUGHostSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return action;

	dialogSetup(me, SLAPD_KEY_UG_HOST, 0);

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askUGHostNext(Dialog *me)
{
	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getBaseScript()->set(SLAPD_KEY_UG_HOST, tmp);

   if (!tmp || !isValid(tmp))
   {
	   DialogManagerType::showAlert("Please enter a valid hostname");
	   return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askUGPort(
"Please specify the port number on which the user directory listens.\n",

"User directory port number",

"389",

askUGPortSetup,
askUGPortNext
);

static DialogAction
askUGPortSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return action;
	
	dialogSetup(me, SLAPD_KEY_UG_PORT, me->defaultAns());

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askUGPortNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return DIALOG_NEXT;
	
   const char *buf = me->input();
   const char *tmp;
   char testbuf[1024];
   int port, err = 0;

   if (buf[0] == 0)
   {
      tmp = me->defaultAns();
   }
   else
   {
      tmp = buf;
   }

   getManager(me)->getBaseScript()->set(SLAPD_KEY_UG_PORT, tmp);

   port = atoi(tmp);
   sprintf(testbuf, "%d", port);
   if (strncmp(testbuf, tmp, 6) || port > MAXPORT || port < 1)
   {
      sprintf(testbuf, "OVERFLOW ERROR: Unable to bind to port %d\n"
                 "Please choose another port between 1 and %d.\n\n",
                   port, MAXPORT);
      err = -1;
   } 

   if (err)
   {
      DialogManagerType::showAlert(testbuf);
      return DIALOG_SAME;
   }

   return DIALOG_NEXT;
}

DialogInput askUGDN(
"In order to add and modify information in the user directory, you must\n"
"be able to bind to the server as an entity with the correct\n"
"permissions.  This user is usually the Directory Manager, although\n"
"other users may be given the proper access.  You will also be asked to\n"
"provide the password.\n",

"User directory administrator ID",

0,

askUGDNSetup,
askUGDNNext
);

static DialogAction
askUGDNSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return action;

	dialogSetup(me, SLAPD_KEY_USER_GROUP_ADMIN_ID, DEFAULT_SLAPDUSER);

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askUGDNNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return DIALOG_NEXT;

	const char *slapdUser;
	char *slapdPwd = 0;
	const char *buf;

	buf = me->input();
	if (buf[0] == 0)
	{
		slapdUser = me->defaultAns();
	}
	else
	{
		slapdUser = buf;
	}

	getManager(me)->getBaseScript()->set(SLAPD_KEY_USER_GROUP_ADMIN_ID, slapdUser);

	if (!isValid(slapdUser))
	{
		DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}

	while (1)
	{
		me->showString("Password: ");
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				free(inp);
				continue;
			}
			else
			{
				slapdPwd = inp;
				break;
			}
		}
	}

	if (slapdPwd)
	{
		getManager(me)->getBaseScript()->set(SLAPD_KEY_USER_GROUP_ADMIN_PWD, slapdPwd);
		free(slapdPwd);

		int status;
		if (status = getManager(me)->verifyRemoteLdap(
									  SLAPD_KEY_UG_HOST,
									  SLAPD_KEY_UG_PORT,
									  SLAPD_KEY_UG_SUFFIX,
									  SLAPD_KEY_USER_GROUP_ADMIN_ID,
									  SLAPD_KEY_USER_GROUP_ADMIN_PWD
			                         )
		   )
		{
			ostrstream msg;
			msg << "Could not connect to ldap://"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_UG_HOST)
				<< ":"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_UG_PORT)
				<< "/"
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_UG_SUFFIX)
				<< endl << "for bind DN "
				<< getManager(me)->getBaseScript()->get(SLAPD_KEY_USER_GROUP_ADMIN_ID)
				<< " status = " << status << endl
				<< "Please check your typing.  If you have mis-typed, you can backup"
				<< endl
				<< "and retype.  Otherwise, the remote server may be down at this time."
				<< endl
				<< "The installation cannot proceed."
				<< endl << ends;
			DialogManagerType::showAlert(msg.str());
			delete [] msg.str();
			return DIALOG_SAME;
		}
	}

	return DIALOG_NEXT;
}

DialogInput askUGSuffix(
"Please specify the suffix for the user directory server.\n",

"User directory server suffix",

NULL,

askUGSuffixSetup,
askUGSuffixNext
);

static DialogAction
askUGSuffixSetup(Dialog *me)
{
	DialogAction action = DIALOG_NEXT;
	long actionval = 0;
	me->getUserData(ACTION, actionval);
	action = (DialogAction)actionval;
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return action;

	dialogSetup(me, SLAPD_KEY_UG_SUFFIX, getManager(me)->getDefaultSuffix());

	long setupval = 0;
	if (me->getUserData(SETUP_DEFAULTS, setupval) == SETUP_ONLY ||
		setupval == SETUP_ONLY)
		return DIALOG_NEXT;

	return DIALOG_SAME;
}

static DialogAction
askUGSuffixNext(Dialog *me)
{
	if (!getManager(me)->featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
		return DIALOG_NEXT;

	const char *buf;
	NSString val;

	buf = me->input();
	if (buf[0] == 0)
		val = me->defaultAns();
	else
		val = buf;

	getManager(me)->getBaseScript()->set(SLAPD_KEY_UG_SUFFIX, val);

	// check to see if it is a valid dn
	if (!isAValidDN(val))
	{
	 	DialogManagerType::showAlert("The suffix must be a valid DN");
	 	return DIALOG_SAME;
	}
	if (!isValid(val))
	{
	 	DialogManagerType::showAlert("Please enter a valid string.");
	 	return DIALOG_SAME;
	}

	return DIALOG_NEXT;
}

DialogInput askReconfigMCAdminPwd(
(const char*)0,
"Brandx configuration directory server\nadministrator ID",

(const char*)0,

askReconfigMCAdminPwdSetup,
askReconfigMCAdminPwdNext
);

static DialogAction
askReconfigMCAdminPwdSetup(Dialog *me)
{
#if DEBUG > 1
	cerr << "Entering askReconfigMCAdminPwdSetup" << endl;
#endif
	NSString msg = NSString(
"In order to reconfigure your installation, the Configuration Directory\n"
"Administrator password is required.  Here is your current information:\n\n"
"Configuration Directory: ") +
		getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_URL) + "\n" +
"Configuration Administrator ID: " +
		getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID) + "\n" +
"\nAt the prompt, please enter the password for the Configuration Administrator.\n";

	me->setText(msg);

	me->setDefaultAns(getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID));
#if DEBUG > 1
	cerr << "Leaving askReconfigMCAdminPwdSetup" << endl;
#endif
	return DIALOG_SAME;
}

static DialogAction
askReconfigMCAdminPwdNext(Dialog *me)
{
	const char *buf;

	buf = me->input();
	if (!buf || buf[0] == 0)
	{
		buf = me->defaultAns();
	}

	getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_ID, buf);

	if (!isValid(buf))
	{
		DialogManagerType::showAlert("Please enter a valid string.");
		return DIALOG_SAME;
	}

	me->showString("Password: ");
	while (1)
	{
		if (me->getPassword () == 0)
		{
			return DIALOG_PREV;
		}
		else
		{
			char *inp = strdup(me->input());

			if (inp[0] == 0)
			{
				me->showString("Password: ");
				continue;
			}
			else if (contains8BitChars(inp))
			{
				DialogManagerType::showAlert("Password must contain 7 bit characters only.");
				return DIALOG_SAME;
			}
			else if (!isValid(inp))
			{
				DialogManagerType::showAlert("Please enter a valid password.");
				return DIALOG_SAME;
			}
			else
			{
				int status;
				if (status = authLdapUser(
					getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_URL),
					getManager(me)->getBaseScript()->get(SLAPD_KEY_SERVER_ADMIN_ID),
					inp, 0, 0))
				{
					ostrstream msg;
					msg << "Could not connect to "
						<< getManager(me)->getBaseScript()->get(SLAPD_KEY_K_LDAP_URL)
						<< endl << "for ID "
						<< getManager(me)->getBaseScript()->get(SLAPD_KEY_USER_GROUP_ADMIN_ID)
						<< " status = " << status << endl
						<< "Please check your typing.  If you have mis-typed, you can backup"
						<< endl
						<< "and retype.  Otherwise, the remote server may be down at this time."
						<< endl
						<< "The reconfiguration cannot proceed."
						<< endl << ends;
					DialogManagerType::showAlert(msg.str());
					delete [] msg.str();
					return DIALOG_SAME;
				}
			}
			getManager(me)->getBaseScript()->set(SLAPD_KEY_SERVER_ADMIN_PWD, inp);
			free(inp);
			break;
		}
	}

	return DIALOG_NEXT;
}

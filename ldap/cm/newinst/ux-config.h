/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*********************************************************************
**
**
** NAME:
**   ux-config.h
**
** DESCRIPTION:
**   Brandx Directory Server Pre-installation Program
**
** NOTES:
**
** HISTORY:
** $Log: ux-config.h,v $
** Revision 1.2  2005/02/02 19:35:43  nhosoi
** [146919] De-brand the Directory Server as "brandx"
**
** Revision 1.1.1.1  2005/01/21 00:40:49  cvsadm
** Moving NSCP Directory Server from DirectoryBranch to TRUNK, initial drop. (foxworth)
**
** Revision 1.1.2.6.8.9  2005/01/14 01:22:10  nhosoi
** For the open-source project.
** 1) eliminated 'netsite' level
** 2) moved ns/config one level lower
** 3) moved fasttime to lib/base
**
** Revision 1.1.2.6.8.8  2003/09/22 19:38:51  ulfw
** Update copyright years from 2001 to 2001-2003
**
** Revision 1.1.2.6.8.7  2001/11/02 23:32:56  richm
** XXX use new copyright XXX
**
** Revision 1.1.2.6.8.6  2001/10/06 20:01:04  richm
** ldapserver/ldap/cm/newinst/ux-config.h
** 1.1.2.6.8.5
** 20010918
**
** Remove copyright caracter form copyright
**
**
** ====================================================
**
** Revision 1.1.2.6.8.5  2001/09/21 15:25:29  richm
** rebrand to Netscape and change version to 6.0
**
** Revision 1.1.2.6.8.4  2001/02/13 09:40:08  rmarco
** copyrights
**
** Revision 1.1.2.6.8.3  2000/08/22 10:07:32  elp
** First bunch of branding fixes.
** Replaced 'Netscape Directory Server' by 'iPlanet Directory Server'.
**
** Revision 1.1.2.6.8.2  2000/08/08 19:34:10  mwahl
** ensure domainname is valid before beginning install
**
** Revision 1.1.2.6.8.1  1999/02/23 02:14:08  ggood
** Merge changes made on server4_directory_branch after 4.0 RTM to DirectoryBranch
**
** Revision 1.1.2.7  1998/11/25 02:07:59  rweltman
** Merging from DS 4.0 RTM into server4_directory_branch
**
** Revision 1.1.2.6.4.2  1998/11/06 21:33:15  richm
** added normalizeDNs
**
** Revision 1.1.2.6.4.1  1998/10/15 18:23:05  richm
** check for bogus admin domain
**
** Revision 1.1.2.6  1998/07/23 21:32:39  richm
** allow re-installation into existing server root
**
** Revision 1.1.2.5  1998/06/15 23:52:08  richm
** added support for user/group separation, better flow control, and support for AS 0611
**
** Revision 1.1.2.4  1997/12/17 21:10:19  richm
** updated for minor 19971216 changes to admin setup sdk
**
** Revision 1.1.2.3  1997/12/06 01:43:18  richm
** upgraded to latest changes from 12.03 admin
**
** Revision 1.1.2.2  1997/11/12 23:42:57  richm
** updates for unix installer
**
** Revision 1.1.2.1  1997/11/04 01:57:53  richm
** Kingpin UNIX installation modules
**
** Revision 1.1.2.4  1997/10/22 02:46:08  pvo
** Removed restore().
**
** Revision 1.1.2.3  1997/10/01 17:24:11  pvo
** Changed include path.
**
** Revision 1.1.2.2  1997/09/27 02:43:39  pvo
** Check in.
**
**
*********************************************************************/
#include "dialog.h"
#include "ux-util.h"
extern const char *DEFAULT_SYSUSER;
extern const char *DEFAULT_OLDROOT;


class SlapdPreInstall:public DialogManager
{
public:

	SlapdPreInstall(int, char **);
	~SlapdPreInstall();

    int init();

    int  start();
    void add (Dialog *);
    void addLast(Dialog *);
	void resetLast();
    void clear();
    int  cont();
    void setParent(void *);
    void *parent() const;

	void setAdminScript(InstallInfo *script);
	InstallInfo *getAdminScript() const;

	InstallInfo *getBaseScript() const;

	int verifyRemoteLdap(const char *host, const char *port, const char *suffix,
						 const char *binddn, const char *binddnpwd) const;

	int verifyAdminDomain(const char *host, const char *port, const char *suffix,
						  const char *admin_domain,
						  const char *binddn, const char *binddnpwd) const;

	const char *getDNSDomain() const;
	const char *getDefaultSuffix() const;
	const char *getConsumerDN() const;
	int featureIsEnabled(const char *which) const;

	static void showAlert(const char *msg);

private:

   NSString        _serverRoot;

   NSString        _infoFile;
   InstallInfo     *_installInfo;
   InstallInfo     *_slapdInfo;
   InstallInfo     *_adminInfo;

   NSString        _logFile;
   InstallLog      *_installLog;

   Bool _configured;
   Bool _reconfig;


   void getOptions(int argc, char **argv);
   int initDefaultConfig();

   void shutdownServers();

   void normalizeDNs();
};

typedef SlapdPreInstall DialogManagerType;

inline DialogManagerType*
getManager(Dialog *me)
{
	return (DialogManagerType*)me->manager();
}


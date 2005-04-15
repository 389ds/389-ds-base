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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
//                                                                          //
//  Name: regparms.h                                                        //
//	Platforms: WIN32                                                        //
//  ......................................................................  //
//  This module contains registry key definations used throughout the       //
//  server.                                                                 //
//  ......................................................................  //
//  Revision History:                                                       //
//  01-12-95  Initial Version, Aruna Victor (aruna@netscape.com)            //
//  12-19-96  3.0 registry changes, Andy Hakim (ahakim@netscape.com)        //
//  07-24-97  3.5 registry changes, Ted Byrd (tbyrd@netscape.com)           //
//  09-28-97  4.0 registry changes, Glen Beasley (gbeasley@netscape.com)    //
//--------------------------------------------------------------------------//
#define KEY_COMPANY             "Fedora"
#define KEY_APP_PATH            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths"
#define KEY_RUN_ONCE            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
#define KEY_SERVICES            "SYSTEM\\CurrentControlSet\\Services"
#define KEY_SNMP_SERVICE        "SNMP\\Parameters\\ExtensionAgents"
#define KEY_SNMP_CURRENTVERSION "SNMP\\CurrentVersion"
#define KEY_EVENTLOG_MESSAGES   "EventLogMessages"
#define KEY_EVENTLOG_APP	    "EventLog\\Application"
#define KEY_SOFTWARE_NETSCAPE   "SOFTWARE\\Fedora"
#define VALUE_IMAGE_PATH        "ImagePath"
#define VALUE_CONFIG_PATH       "ConfigurationPath"
#define VALUE_ROOT_PATH         "RootPath"
#define VALUE_APP_PATH          "Pathname"
#define PROGRAM_GROUP_NAME      "Fedora SuiteSpot"
#define STR_PRODUCT_TYPE        "Server"
#define STR_EXE                 ".exe"
#define STR_COMPANY_PREFIX      "ns-"

/* SuiteSpot IDs */
#define NSS_NAME_SHORT         "SuiteSpot"
#define NSS_VERSION            "6.0"
#define NSS_NAME_VERSION       "SuiteSpot 6.0"
#define NSS_NAME_FULL          "Fedora SuiteSpot"
#define NSS_NAME_FULL_VERSION  "Fedora SuiteSpot 6.0"
#define NSS_NAME_UNINSTALL     "Uninstall SuiteSpot 6.0"

/* Admin IDs */
#define ADM_ID_PRODUCT         "admin"
#define ADM_NAME_SHORT         "Administration"
#define ADM_VERSION            "7.0"
#define ADM_NAME_VERSION       "Administration 7.0"
#define ADM_NAME_SERVER        "Administration Server"
#define ADM_NAME_FULL          "Fedora Administration Server"
#define ADM_NAME_FULL_VERSION  "Fedora Administration Server 7.0"
#define ADM_NAME_SERVICE       "Fedora Administration 7.0"
#define ADM_EXE                "ns-admin.exe"
#define ADM_EXE_START          "admin.exe"
#define ADM_ID_SERVICE         "admin70"
#define ADM_KEY_ROOT           "Administration\\7.0"
#define ADM_SERVER_LST_NAME    "adm:Netscape Enterprise Server"
#define ADM_DIR_ROOT           "admin"
#define ADM_NAME_UNINSTALL     "Uninstall Administration Server 7.0"

#if defined( NS_DS )
#define ADMIN_SERVICE_NAME      "Admin Server" 
#define ADMIN_ICON_NAME          "Administer Netscape Servers"
#endif

/* Enterprise IDs */
#define ENT_ID_PRODUCT         "https"
#define ENT_NAME_SHORT         "Enterprise"
#define ENT_VERSION            "6.2"
#define ENT_NAME_VERSION       "Enterprise 6.2"
#define ENT_NAME_SERVER        "Enterprise Server"
#define ENT_NAME_FULL          "Fedora Enterprise Server"
#define ENT_NAME_FULL_VERSION  "Fedora Enterprise Server 6.2"
#define ENT_NAME_SERVICE       "Fedora Enterprise 6.2"
#define ENT_EXE                "ns-httpd.exe"
#define ENT_EXE_START          "httpd.exe"
#define ENT_ID_SERVICE         "https"
#define ENT_KEY_ROOT           "Enterprise\\6.2"
#define ENT_SERVER_LST_NAME    "https:Fedora Enterprise Server"
#define ENT_DIR_ROOT           "https"
#define ENT_NAME_UNINSTALL     "Uninstall Enterprise Server 3.01"

#if 0
/* Personal IDs */
#define PERSONAL_APP_PATH_KEY           "ns-httpd.exe"
#define PERSONAL_README_ICON_NAME       "FastTrack README"
#define PERSONAL_REGISTRY_ROOT_KEY      "Httpd Server"
#define PERSONAL_SERVER_LST_NAME        "httpd:Netscape FastTrack Server"
#define PERSONAL_UNINSTALL_ICON_NAME    "Uninstall FastTrack"
#define PERSONAL_UNINSTALL_KEY          "FastTrackV2.0"
#define PERSONAL_SERVER_NAME            "Netscape FastTrack Server"

#define PER_ID_PRODUCT         "httpd"
#define PER_NAME_SHORT         "FastTrack"
#define PER_VERSION            "3.01"
#define PER_NAME_VERSION       "FastTrack 3.01"
#define PER_NAME_SERVER        "FastTrack Server"
#define PER_NAME_FULL          "Netscape FastTrack Server"
#define PER_NAME_FULL_VERSION  "Netscape FastTrack Server 3.01"
#define PER_NAME_SERVICE       "Netscape FastTrack 3.01"
#define PER_EXE                "ns-httpd.exe"
#define PER_EXE_START          "httpd.exe"
#define PER_ID_SERVICE         "httpd"
#define PER_KEY_ROOT           "FastTrack\\3.01"
#define PER_SERVER_LST_NAME    "httpd:Netscape FastTrack Server"
#define PER_DIR_ROOT           "httpd"
#define PER_NAME_UNINSTALL     "Uninstall FastTrack Server 3.01"

/* Proxy IDs */
#define PRX_ID_PRODUCT         "proxy"
#define PRX_NAME_SHORT         "Proxy"
#define PRX_VERSION            "3.0"
#define PRX_NAME_VERSION       "Proxy 3.0"
#define PRX_NAME_SERVER        "Proxy Server"
#define PRX_NAME_FULL          "Netscape Proxy Server"
#define PRX_NAME_FULL_VERSION  "Netscape Proxy Server 3.0"
#define PRX_NAME_SERVICE       "Netscape Proxy 3.0"
#define PRX_EXE                "ns-proxy.exe"
#define PRX_EXE_START          "proxy.exe"
#define PRX_ID_SERVICE         "proxy30"
#define PRX_KEY_ROOT           "Proxy\\3.0"
#define PRX_SERVER_LST_NAME    "proxy:Netscape Proxy Server"
#define PRX_DIR_ROOT           "proxy"
#define PRX_NAME_UNINSTALL     "Uninstall Proxy Server 3.0"

/* Catalog IDs */
#define CATALOG_SHORT_NAME             "Catalog"
#define CATALOG_SERVER_NAME            "Netscape Catalog Server"
#define CATALOG_SERVER_VERSION         "1.0"
#define CATALOG_SETUP_SHORT_NAME       "Catalog Server"
#define CATALOG_SETUP_NAME             "Netscape Catalog Server 1.0"
#define CATALOG_REGISTRY_ROOT_KEY      "Catalog Server"
#define CATALOG_EXE                    "ns-httpd.exe"
#define CATALOG_DIR_ROOT               "catalog"
#define CATALOG_APP_PATH_KEY           "ns-catalog"
#define CATALOG_UNINSTALL_KEY          "CatalogV1.0"
#define CATALOG_SERVER_LST_NAME        "catalog:Netscape Catalog Server"
#define CATALOG_SERVICE_PREFIX         "Netscape Catalog Server "
#define CATALOG_README_ICON_NAME       "Catalog README"
#define CATALOG_UNINSTALL_ICON_NAME    "Uninstall Catalog"
#define CATALOG_PRODUCT_NAME           "catalog"

/* RDS IDs */
#define RDS_SHORT_NAME             "RDS"
#define RDS_SERVER_NAME            "Netscape RDS Server"
#define RDS_SERVER_VERSION         "1.0"
#define RDS_SETUP_SHORT_NAME       "RDS Server"
#define RDS_SETUP_NAME             "Netscape RDS Server 1.0"
#define RDS_REGISTRY_ROOT_KEY      "RDS Server"
#define RDS_EXE                    "ns-httpd.exe"
#define RDS_DIR_ROOT               "rds"
#define RDS_APP_PATH_KEY           "ns-rds"
#define RDS_UNINSTALL_KEY          "RdsV1.0"
#define RDS_SERVER_LST_NAME        "rds:Netscape RDS Server"
#define RDS_SERVICE_PREFIX         "Netscape RDS Server "
#define RDS_README_ICON_NAME       "Rds README"
#define RDS_UNINSTALL_ICON_NAME    "Uninstall RDS"
#define RDS_PRODUCT_NAME           "rds"

/* News IDs */
#define NEWS_SHORT_NAME           "News"
/* Alpha #define NEWS_SERVER_NAME          "Netscape News Server (tm) " */
/* Alpha #define NEWS_SETUP_NAME           "Netscape News Server (tm) " */
/* Alpha #define NEWS_UNINSTALL_KEY        "NewsV1.2" */
#define NEWS_SERVER_NAME          "Netscape News Server"
#define NEWS_SERVER_VERSION       "2.0"
#define NEWS_UNINSTALL_KEY        "NetscapeNewsV2.0"
#define NEWS_SETUP_SHORT_NAME     "News Server"
#define NEWS_SETUP_NAME           "Netscape News Server"
#define NEWS_REGISTRY_ROOT_KEY    "News Server" // key under SW/Netscape
#define NEWS_EXE                  "nnrpd.exe" // value for <No name>
#define NEWS_DIR_ROOT             "news"     // mess.dll in Reg, and in .lst
#define NEWS_APP_PATH_KEY         "innd.exe" // key under app paths
#define NEWS_SERVER_LST_NAME      "news:Netscape News Server"
#define NEWS_SERVICE_PREFIX       "Netscape News Server "
#define NEWS_README_ICON_NAME     "News Readme"
#define NEWS_UNINSTALL_ICON_NAME  "Uninstall News"

/* Mail IDs */
/* When we integrate the core & admin servers installation processes */
/* we will use the code below instead of the section following it.   */

/*
#define MAIL_SHORT_NAME           "Mail"
#define MAIL_SERVER_NAME          "Netscape Mail Server (tm)"
#define MAIL_SERVER_VERSION       "2.0"
#define MAIL_SETUP_SHORT_NAME     "Mail Server"
#define MAIL_SETUP_NAME           "Netscape Mail Server (tm)"
#define MAIL_REGISTRY_ROOT_KEY    "Mail Server"     // key under SW/Netscape
#define MAIL_EXE                  "NetscapeMTA.exe" // value for <No name>
#define MAIL_DIR_ROOT             "mail"            // mess.dll in Reg, and in .lst
#define MAIL_APP_PATH_KEY         "NetscapeMTA.exe" // key under app paths
#define MAIL_UNINSTALL_KEY        "MailV2.0"
#define MAIL_SERVER_LST_NAME      "mail:Netscape Mail Server"
#define MAIL_SERVICE_PREFIX       "Netscape Mail Server "
#define MAIL_README_ICON_NAME     "Mail Readme"
#define MAIL_UNINSTALL_ICON_NAME  "Uninstall Mail"
*/

#define MAIL_SHORT_NAME           "Admin"
#define MAIL_SERVER_NAME          "Netscape Administration Server (tm)"
#define MAIL_SERVER_VERSION       "2.0"
#define MAIL_SETUP_SHORT_NAME     "Admin Server"
#define MAIL_SETUP_NAME           "Netscape Administration Server (tm)"
#define MAIL_REGISTRY_ROOT_KEY    "Mail Server"     // key under SW/Netscape
#define MAIL_EXE                  "NetscapeMTA.exe" // value for <No name>
#define MAIL_DIR_ROOT             "mail"            // mess.dll in Reg, and in .lst
#define MAIL_APP_PATH_KEY         "NetscapeMTA.exe" // key under app paths
#define MAIL_UNINSTALL_KEY        "MailV2.0"
#define MAIL_SERVER_LST_NAME      "mail:Netscape Mail Server"
#define MAIL_SERVICE_PREFIX       "Netscape Admin Server "
#define MAIL_README_ICON_NAME     "Mail Readme"
#define MAIL_UNINSTALL_ICON_NAME  "Uninstall Mail"
#endif

/* Synchronization Service IDs */
#define DSS_SHORT_NAME           "Directory Synchronization Service"
#define DSS_SERVER_NAME          "Fedora Directory Synchronization Service"
#define DSS_SERVER_VERSION       "7"
#define DSS_SETUP_SHORT_NAME     "Fedora Synchronization Service"
#define DSS_SETUP_NAME           "Fedora Directory Synchronization Service 7"
#define DSS_REGISTRY_ROOT_KEY    "Directory Synchronization Service"
#define DSS_EXE                  "dssynch.exe"
#define DSS_DIR_ROOT             "dssynch"
#define DSS_APP_PATH_KEY         "dssynch.exe"
#define DSS_CONFIG_TOOL          "synchcfg.exe"
#define DSS_UNINSTALL_KEY        "SynchronizationV7"
#define DSS_SERVER_LST_NAME      "dssynch:Netscape Directory Synchronization Service"
#define DSS_SERVICE_PREFIX       "Fedora Directory Synchronization Service "
#define DSS_README_ICON_NAME     "Directory Synchronization Service README"
#define DSS_CONFIG_ICON_NAME     "Directory Synchronization Service Config"
#define DSS_UNINSTALL_ICON_NAME  "Uninstall Directory Synch Service"
#define DSS_PRODUCT_NAME         "dssynch"
#define DSS_ID_PRODUCT			 DSS_PRODUCT_NAME
#define DS_COMPONENT             1

/* IDs needed for Directory 102/30 synchservice */
#define ADMIN_APP_PATH_KEY       "ns-admin.exe"
#define ADMIN_EXE               "ns-admin.exe"
#define ADMIN_REGISTRY_ROOT_KEY "admin.exe"
#define ADMSERV_COMPRESSED_FILE	"admserv.z"
#define APPBASE_DIR95           "Program Files"
#define APPBASE_PATH            "Netscape"
#define APP_PATH_KEY            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths"
#define BASE_REGISTRY95         "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"
#define BASE_REGISTRYNT         "Software\\Microsoft\\Windows NT\\CurrentVersion\\App Paths\\"
#define CMS_COMPRESSED_FILE     "certsvr.z"
#define CMS_DIR_ROOT             "cms"
#define CMS_SHORT_NAME          "Certificate Server"
#define CMS_APP_PATH_KEY         "libcms.dll"
#define CMS_UNINSTALL_KEY        "CertificateV1.0"
#define CMS_UNINSTALL_ICON_NAME  "Uninstall CertServer"
#define UPGRADE_VER_1_ICON_NAME "Upgrade 1.1x Servers"
#define CMS_README_ICON_NAME     "CertServer README"
#define CMS_REGISTRY_ROOT_KEY    "Certificate Server"
#define CMS_SERVER_NAME          "Netscape Certificate Server"
#define CMS_SERVER_LST_NAME      "cms:Netscape Certificate Server"
#define COMPANY_NAME            "Netscape"
#define DIR_HTTPD_SERVER         DSS_DIR_ROOT
#define DSS_COMPRESSED_FILE     "dssynch.z"
#define DSS_COMPRESSED_HELP_FILE "hdssynch.z"
#define DS_COMPRESSED_FILE      "slapd.z"
#define ENTERPRISE_APP_PATH_KEY         "ns-https.exe"
#define ENTERPRISE_README_ICON_NAME     "Enterprise README"
#define ENTERPRISE_REGISTRY_ROOT_KEY    "Https Server"
#define ENTERPRISE_SERVER_LST_NAME      "https:Netscape Enterprise Server"
#define ENTERPRISE_SERVER_NAME          "Netscape Enterprise Server"
#define ENTERPRISE_UNINSTALL_KEY        "EnterpriseV2.0"
#define ENTERPRISE_UNINSTALL_ICON_NAME  "Uninstall Enterprise"
#define ENTERPRISE_DIR_ROOT     "https"
#define ENTERPRISE_SHORT_NAME   "Enterprise"
#define EXTRAS_COMPRESSED_FILE	"extras.z"
#define HTTP_SERVER_NAME         DSS_DIR_ROOT 
#define INSTALL_COMPRESSED_FILE	"install.z"
#define LIVEWIRE_COMPRESSED_FILE "wire.z"
#define NSAPI_COMPRESSED_FILE	 "nsapi.z"
#define PERSONAL_DIR_ROOT        "httpd"
#define PERSONAL_SHORT_NAME      "FastTrack"
#define PLUGINS_COMPRESSED_FILE	 "plugins.z"
#define REGISTRY_ROOT_PATH_KEY   "Path"
#define SERVDLLS_COMPRESSED_FILE "servdlls.z"
#define SERVER_APP_PATH_KEY      DSS_APP_PATH_KEY
#define SERVER_COMPRESSED_FILE	"server.z"
#define SERVER_EXE               DSS_EXE
#define SERVER_LIST_NAME         DSS_SERVER_LST_NAME
#define SERVER_PRODUCT_NAME      DSS_REGISTRY_ROOT_KEY
#define SERVER_PRODUCT_VERSION   DSS_VERSION_DEF
#define SERVER_README_ICON_NAME  DSS_README_ICON_NAME
#define SERVER_UNINSTALL_ICON_NAME DSS_UNINSTALL_ICON_NAME
#define SETUP_NAME               DSS_SETUP_NAME
#define SETUP_SHORT_NAME         DSS_SHORT_NAME
#define SETUP_TITLE_WIN95_BMP	"titleNTb.bmp"
#define SETUP_TITLE_WINNT_BMP	"titledss.bmp"
#define SOFTWARE_NETSCAPE_KEY   "SOFTWARE\\Netscape"
#define NETSCAPE_WEB_KEY	"Netscape Web Servers"
#define NETSCAPE_SERVICE_KEY	"SYSTEM\\CurrentControlSet\\Services"

#define SYSDLLS_COMPRESSED_FILE	"ssdlls.z"
#define UNINSTALL_KEY            DSS_UNINSTALL_KEY
#define UNINSTALL_REGISTRY95    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
// NT SNMP Extension Agent registry entries
#define SNMP_SERVICE_KEY       "SYSTEM\\CurrentControlSet\\Services\\SNMP\\Parameters\\ExtensionAgents\\"
#define SNMP_AGENT_KEY         "HTTP SNMP Agent"
#define SNMP_CURRENT_VERSION   "CurrentVersion"
#define SNMP_PATHNAME			 "Pathname"
#define SNMP_DLL_PATH          "bin\\https\\httpsnmp.dll"
#define SNMP_SERVICE_NAME      "SNMP"
// end synch service

#define LICENSE_TXT             "license.txt"

#define UNINST_EXE              "unslapd.exe"
#define DSS_UNINST_EXE          "unsynch.exe"

#define DIR_ADMSERV_SERVER      "admserv"

#define COPY_READMEFILES         "Copying readme files..."
#define COPY_SYSDLLFILES         "Copying Shared System files..."
#define COPY_SERVDLLFILES        "Copying Shared Server files..."
#define COPY_SERVERFILES         "Copying web server files..."
#define COPY_ADMINFILES          "Copying administration server files..."
#define COPY_EXTRASFILES         "Copying CGI Example and Log Analyzer Files..."
#define COPY_INSTALLFILES        "Copying Version 1.1x upgrade files..."
#define COPY_NSAPIFILES          "Copying NSAPI Library and Examples Files..."
#define COPY_PLUGINSFILES        "Copying Plug-in Files..."
#define COPY_LIVEWIREFILES       "Copying LiveWire Files..."
#define INSTALL_LIVEWIREFILES    "Installing LiveWire Server Extension files..."
#define INSTALL_CMSFILES	 "Installing Certificate Server files..."
#define INSTALL_DSFILES 	 "Installing Directory Server files..."
#define INSTALL_DSSFILES 	 "Installing Directory Synchronization Service files..."
#define INSTALL_DSSHELPFILES 	 "Installing Directory Synchronization Service Help files..."

#define STR_DEFTAB              "            "
/* end temp ds102 IDs */


/* Directory IDs */
/* NOTES:
   dboreham: I have no idea what is going on below:
   we seem to be using two completely different sets of defines.
   This needs sorted out 
   ryamaura: The first group is there only to ensure that 
   nothing breaks. The second group conforms to the rest of
   the SuiteSpot servers and should be the final form.
*/
#define DS_SHORT_NAME           "Directory Server"
#define DS_SERVER_NAME          "Fedora Directory Server"
#define DS_SERVER_VERSION       "7"
#define DS_SETUP_SHORT_NAME     "Directory Server"
#define DS_SETUP_NAME           "Fedora Directory Server 7"
#define DS_REGISTRY_ROOT_KEY    "Directory Server"
#define DS_APP_PATH_KEY         "ns-slapd.exe"
#define DS_UNINSTALL_KEY        "DirectoryV7"
#define DS_SERVICE_PREFIX       "Fedora Directory Server "
#define DS_README_ICON_NAME     "Directory Server 7 README"
#define DS_UNINSTALL_ICON_NAME  "Uninstall Directory Server 7"
#define DS_PRODUCT_NAME         "slapd"

#define DS_ID_PRODUCT           "slapd"
#define DS_NAME_SHORT           "Directory"
#define DS_VERSION_OLD          "3.0"
#undef  DS_VERSION
#define DS_VERSION              "7"
#define DS_NAME_VERSION         "Directory 7"
#define DS_NAME_SERVER          "Directory Server"
#define DS_NAME_FULL            "Fedora Directory Server"
#define DS_NAME_FULL_VERSION    "Fedora Directory Server 7"
#define DS_NAME_SERVICE         "Fedora Directory 7"
#define DS_EXE                  "ns-slapd.exe"
#define DS_EXE_START            "slapd.exe"
#define DS_ID_SERVICE           "slapd"
#define DS_KEY_ROOT             "Directory\\7"
#define DS_KEY_ROOT_OLD         "Directory\\3.0"
#define DS_SERVER_LST_NAME      "slapd:Fedora Directory Server"
#define DS_DIR_ROOT             "slapd"
#define DS_NAME_UNINSTALL       "Uninstall Directory Server 7"
#define DS_SNMP_PATH            "bin\\slapd\\server\\ns-ldapagt.dll"
#define DS_OPTIONS              "Select the installation option from below"
#define DS_OPTIONS_TITLE 		"Directory Server Installions Options"
#define DS_GENERAL_OPTIONS       DS_NAME_SERVER 


#ifndef DS_COMPONENT
#define DS_COMPONENT          1
#endif

/* original definitions */
// Upper-level registry parameters
/* Note: the followin MCC_ are not defined when this file is included
   for the NT setup.rul file. Beware of using the following definitions in NT
   - nirmal
*/
#if defined(MCC_ADMSERV)

#define SERVICE_NAME           ADM_ID_SERVICE
#define EVENTLOG_APPNAME	   ADM_NAME_VERSION
#define SERVICE_EXE            ADM_EXE
#define SERVICE_PREFIX         ADM_NAME_VERSION
#define SVR_ID_PRODUCT         ADM_ID_PRODUCT
#define SVR_NAME_SHORT         ADM_NAME_SHORT
#define SVR_VERSION            ADM_VERSION
#define SVR_NAME_VERSION       ADM_NAME_VERSION
#define SVR_NAME_SERVER        ADM_NAME_SERVER
#define SVR_NAME_FULL          ADM_NAME_FULL
#define SVR_NAME_FULL_VERSION  ADM_NAME_FULL_VERSION
#define SVR_NAME_SERVICE       ADM_NAME_SERVICE
#define SVR_EXE                ADM_EXE
#define SVR_EXE_START          ADM_EXE_START
#define SVR_ID_SERVICE         ADM_ID_SERVICE
#define SVR_KEY_ROOT           ADM_KEY_ROOT
#define SVR_SERVER_LST_NAME    ADM_SERVER_LST_NAME
#define SVR_DIR_ROOT           ADM_DIR_ROOT
#define SVR_NAME_UNINSTALL     ADM_NAME_UNINSTALL

#elif defined(NS_ENTERPRISE)

#define PRODUCT_KEY            ENT_KEY_ROOT
#define PRODUCT_NAME           ENT_ID_PRODUCT
#define EVENTLOG_APPNAME       ENT_NAME_VERSION
#define SERVICE_PREFIX         ENT_NAME_VERSION
#define SVR_ID_PRODUCT         ENT_ID_PRODUCT
#define SVR_NAME_SHORT         ENT_NAME_SHORT
#define SVR_VERSION            ENT_VERSION
#define SVR_NAME_VERSION       ENT_NAME_VERSION
#define SVR_NAME_SERVER        ENT_NAME_SERVER
#define SVR_NAME_FULL          ENT_NAME_FULL
#define SVR_NAME_FULL_VERSION  ENT_NAME_FULL_VERSION
#define SVR_NAME_SERVICE       ENT_NAME_SERVICE
#define SVR_EXE                ENT_EXE
#define SVR_EXE_START          ENT_EXE_START
#define SVR_ID_SERVICE         ENT_ID_SERVICE
#define SVR_KEY_ROOT           ENT_KEY_ROOT
#define SVR_SERVER_LST_NAME    ENT_SERVER_LST_NAME
#define SVR_DIR_ROOT           ENT_DIR_ROOT
#define SVR_NAME_UNINSTALL     ENT_NAME_UNINSTALL

#elif defined(NS_PROXY)

#define PRODUCT_KEY            PRX_KEY_ROOT
#define PRODUCT_NAME           PRX_ID_PRODUCT
#define EVENTLOG_APPNAME       PRX_NAME_VERSION
#define SERVICE_PREFIX         PRX_NAME_VERSION
#define SVR_ID_PRODUCT         PRX_ID_PRODUCT
#define SVR_NAME_SHORT         PRX_NAME_SHORT
#define SVR_VERSION            PRX_VERSION
#define SVR_NAME_VERSION       PRX_NAME_VERSION
#define SVR_NAME_SERVER        PRX_NAME_SERVER
#define SVR_NAME_FULL          PRX_NAME_FULL
#define SVR_NAME_FULL_VERSION  PRX_NAME_FULL_VERSION
#define SVR_NAME_SERVICE       PRX_NAME_SERVICE
#define SVR_EXE                PRX_EXE
#define SVR_EXE_START          PRX_EXE_START
#define SVR_ID_SERVICE         PRX_ID_SERVICE
#define SVR_KEY_ROOT           PRX_KEY_ROOT
#define SVR_SERVER_LST_NAME    PRX_SERVER_LST_NAME
#define SVR_DIR_ROOT           PRX_DIR_ROOT
#define SVR_NAME_UNINSTALL     PRX_NAME_UNINSTALL

#elif defined(NS_CATALOG)

#define PRODUCT_KEY         CATALOG_REGISTRY_ROOT_KEY // CKA (should use key above)
#define PRODUCT_NAME        "catalog"
#define EVENTLOG_APPNAME	"NetscapeCatalog"
#define SERVICE_PREFIX      CATALOG_SERVICE_PREFIX

#elif defined(NS_RDS)

#define PRODUCT_KEY         RDS_REGISTRY_ROOT_KEY // CKA (should use key above)
#define PRODUCT_NAME        "rds"
#define EVENTLOG_APPNAME	"NetscapeRds"
#define SERVICE_PREFIX      RDS_SERVICE_PREFIX

#elif defined(NS_PERSONAL)

#define PRODUCT_KEY            PER_KEY_ROOT
#define PRODUCT_NAME           PER_ID_PRODUCT
#define EVENTLOG_APPNAME       PER_NAME_VERSION
#define SERVICE_PREFIX         PER_NAME_VERSION
#define SVR_ID_PRODUCT         PER_ID_PRODUCT
#define SVR_NAME_SHORT         PER_NAME_SHORT
#define SVR_VERSION            PER_VERSION
#define SVR_NAME_VERSION       PER_NAME_VERSION
#define SVR_NAME_SERVER        PER_NAME_SERVER
#define SVR_NAME_FULL          PER_NAME_FULL
#define SVR_NAME_FULL_VERSION  PER_NAME_FULL_VERSION
#define SVR_NAME_SERVICE       PER_NAME_SERVICE
#define SVR_EXE                PER_EXE
#define SVR_EXE_START          PER_EXE_START
#define SVR_ID_SERVICE         PER_ID_SERVICE
#define SVR_KEY_ROOT           PER_KEY_ROOT
#define SVR_SERVER_LST_NAME    PER_SERVER_LST_NAME
#define SVR_DIR_ROOT           PER_DIR_ROOT
#define SVR_NAME_UNINSTALL     PER_NAME_UNINSTALL

#elif defined(NS_DSS)

#define PRODUCT_KEY         DSS_REGISTRY_ROOT_KEY // CKA (should use key above)
#define PRODUCT_NAME        "dssynch"
#define EVENTLOG_APPNAME	"NetscapeDirSynchService"
#define SERVICE_PREFIX      DSS_SERVICE_PREFIX

#elif defined(NS_DS)

#define PRODUCT_BIN         "ns-slapd"
#define SLAPD_EXE           "slapd.exe"
#define SERVICE_EXE         SLAPD_EXE
#define SLAPD_CONF          "slapd.conf"
#define SLAPD_DONGLE_FILE   "password.dng"
#define DONGLE_FILE_NAME    SLAPD_DONGLE_FILE

#define PRODUCT_KEY            DS_REGISTRY_ROOT_KEY 
#define PRODUCT_NAME           DS_ID_PRODUCT
#define EVENTLOG_APPNAME           DS_NAME_VERSION
#define SERVICE_PREFIX         DS_NAME_VERSION
#define SVR_ID_PRODUCT         DS_ID_PRODUCT       
#define SVR_NAME_SHORT         DS_NAME_SHORT       
#define SVR_VERSION            DS_VERSION          
#define SVR_VERSION_OLD        DS_VERSION_OLD
#define SVR_NAME_VERSION       DS_NAME_VERSION     
#define SVR_NAME_SERVER        DS_NAME_SERVER      
#define SVR_NAME_FULL          DS_NAME_FULL        
#define SVR_NAME_FULL_VERSION  DS_NAME_FULL_VERSION
#define SVR_NAME_SERVICE       DS_NAME_SERVICE
#define SVR_EXE                DS_EXE
#define SVR_EXE_START          DS_EXE_START
#define SVR_ID_SERVICE         DS_ID_SERVICE
#define SVR_KEY_ROOT           DS_KEY_ROOT
#define SVR_SERVER_LST_NAME    DS_SERVER_LST_NAME
#define SVR_DIR_ROOT           DS_DIR_ROOT
#define SVR_NAME_UNINSTALL     DS_NAME_UNINSTALL
#define SNMP_PATH              DS_SNMP_PATH

#elif defined(NS_SETUP)
#else

#error SERVER TYPE NOT DEFINED

#endif /* MCC_ADMSERV */

// Do not move this section. This has to come immediately after the
//  ifdef section above - Nirmal
//
#if defined(MCC_NEWS)	// Nirmal : added for news 2/21/95.
#define PRODUCT_BIN	    "innd"    // Redefine the generic ns-httpd.exe
#define PRODUCT_KEY         NEWS_REGISTRY_ROOT_KEY // CKA (should use key above)
#define PRODUCT_NAME        "news"
#define EVENTLOG_APPNAME    "NetscapeNews"
#define SERVICE_PREFIX      NEWS_SERVICE_PREFIX

#endif


#define VERSION_KEY "CurrentVersion"

// Configuration Parameters

#define SOFTWARE_KEY                    "Software"

// NT Perfmon DLL entries
#define KEY_PERFORMANCE     "Performance"
#define PERF_MICROSOFT_KEY	"SOFTWARE\\Microsoft\\Windows NT\\Perflib\\009"
#define PERF_COUNTER_KEY	"Counter"
#define PERF_HELP_KEY		"Help"
#define PERF_OPEN_FUNCTION	"OpenNSPerformanceData"
#define PERF_COLLECT_FUNCTION	"CollectNSPerformanceData"
#define PERF_CLOSE_FUNCTION	"CloseNSPerformanceData"
#define PERF_CTR_INI		"nsctrs.ini"

// this section used to be in confhttp.h.  TODO: convert to SVR_ format -ahakim
#if defined(NS_CATALOG)
#define SERVER_REGISTRY_ROOT_KEY    CATALOG_REGISTRY_ROOT_KEY
#define SERVER_APP_PATH_KEY         CATALOG_APP_PATH_KEY
#define SERVER_DIR_ROOT             CATALOG_DIR_ROOT
#define SERVER_SETUP_NAME           CATALOG_SETUP_NAME
#define SERVER_SHORT_NAME           CATALOG_SHORT_NAME

#elif defined(NS_RDS)
#define SERVER_REGISTRY_ROOT_KEY    RDS_REGISTRY_ROOT_KEY
#define SERVER_APP_PATH_KEY         RDS_APP_PATH_KEY
#define SERVER_DIR_ROOT             RDS_DIR_ROOT
#define SERVER_SETUP_NAME           RDS_SETUP_NAME
#define SERVER_SHORT_NAME           RDS_SHORT_NAME

#endif

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dirlite_strings.h - strings  used for Directory Lite */
#ifndef _DIRLITE_STRINGS_H_
#define _DIRLITE_STRINGS_H_

#define LITE_PRODUCT_NAME "restricted-mode directory"
#define LITE_UPGRADE_BLURB "To gain access to this feature, you must upgrade to the full verson of the directory."

#define LITE_GENERIC_ERR "cannot be configured in the " LITE_PRODUCT_NAME ". " LITE_UPGRADE_BLURB



/* Directory Lite: Error Strings related to configuring replication */
#define LITE_CHANGELOG_DIR_ERR     "Error: changelog cannot be configured in DirectoryLite."
#define LITE_CHANGELOG_SUFFIX_ERR  "Error: changelogsuffix cannot be configured in DirectoryLite."
#define LITE_CHANGELOG_MAXAGE_ERR  "Error: changelogmaxage cannot be configured in DirectoryLite."
#define LITE_CHANGELOG_MAXENTRIES_ERR "Error: changelogmaxentries cannot be configured in DirectoryLite."
#define LITE_REPLICATIONDN_ERR "Error: replicationdn cannot be configured in DirectoryLite."
#define LITE_REPLICATIONPW_ERR "Error: replicationpw cannot be configured in DirectoryLite."



/* Directory Lite: Error Strings related to configurating referrals */
#define LITE_DEFAULT_REFERRAL_ERR "Error: Referrals are disabled in the " LITE_PRODUCT_NAME ", The defaultreferral " LITE_GENERIC_ERR

#define LITE_REFERRAL_MODE_ERR "Error: Referrals are disabled in the " LITE_PRODUCT_NAME ", The referralmode " LITE_GENERIC_ERR

/* Directory Lite: Error Strings related to configuring password policy */
#define LITE_PW_EXP_ERR "Error: password policy is disabled in the " LITE_PRODUCT_NAME ", pw_exp " LITE_GENERIC_ERR

/* all plugins which need to be used for Directory Lite must use this as their vendor string */
#define PLUGIN_MAGIC_VENDOR_STR "Netscape Communications Corp."

/* plugins which contain this substring in their pluginid will not be aprroved in DS Lite */
#define LITE_NTSYNCH_PLUGIN_ID_SUBSTR "nt-sync"

/*Directory Lite: Error Strings related to configuring nt synch service */
#define LITE_NTSYNCH_ERR "Error: NT Synch Service " LITE_GENERIC_ERR " nt_synch cannot be enabled."

#define LITE_DISABLED_ATTRS_DN    "cn=attributes,cn=options,cn=features,cn=config"
#define LITE_DISABLED_MODULES_DN  "cn=modules,cn=options,cn=features,cn=config"

#define LITE_REPLICA_ERR "Error: Replication is disabled in the " LITE_PRODUCT_NAME ", replica " LITE_GENERIC_ERR

/*Directory Lite: Error Strings related to configuring maxdescriptors */
#define LITE_MAXDESCRIPTORS_ERR "Warning: The maximum number of concurent connections to the " LITE_PRODUCT_NAME " is 256. Maxdescriptors has a maximum value of 256, setting value for maxdescriptors to 256. To increase the maximum number of concurent connections, you must upgrade to the full version of the directory."
#define SLAPD_LITE_MAXDESCRIPTORS 256

/* on-line backup and restore */
#define LITE_BACKUP_ERR "Error: The " LITE_PRODUCT_NAME " server must be in readonly mode before you can do this operation. You must upgrade to the full version of the directory to be able to perform online backup without first putting the server into readonly mode."

/* Directory Lite: Error string related to enabling third party plugins */
#define LITE_3RD_PARTY_PLUGIN_ERR "Error: Plugins written by third parties are disabled in " LITE_PRODUCT_NAME ". Plugin \"%s\" is disabled. " LITE_UPGRADE_BLURB

#endif /* _DIRLITE_STRINGS_H_ */




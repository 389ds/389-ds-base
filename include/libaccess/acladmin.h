/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __acladmin_h
#define __acladmin_h


/*
 * Description (acladmin.h)
 *
 *	This file describes the interface to access control list (ACL)
 *	administration functions.  This interface provides mechanisms
 *	for inspecting, modifying, and writing out in text form ACL
 *	structures.
 */

#include "aclstruct.h"

NSPR_BEGIN_EXTERN_C

/* Flags used for various functions */
#define ACLF_NPREFIX	0x1		/* ACL name string is a name prefix */
#define ACLF_REXACT	0x2		/* rights must match exactly */
#define ACLF_RALL	0x4		/* must have all specified rights */

/* Functions in acladmin.c */
extern NSAPI_PUBLIC int aclDNSAddHost(char * newhost,
				      char ***alist, int * asize);
extern NSAPI_PUBLIC int aclDNSAddAliases(char * host,
					 char ***alist, int * asize);
extern NSAPI_PUBLIC int aclDNSPutHost(char * hname, int fqdn, int aliases,
				      char ***alist, int * asize);
extern NSAPI_PUBLIC int aclFindByName(ACContext_t * acc, char * aclname,
			 char **rights, int flags, ACL_t **pacl);
extern NSAPI_PUBLIC char * aclGetAuthMethod(ACL_t * acl, int dirno);
extern NSAPI_PUBLIC char * aclGetDatabase(ACL_t * acl, int dirno);
extern NSAPI_PUBLIC char **aclGetHosts(ACL_t * acl, int dirno, int clsno);
extern NSAPI_PUBLIC char * aclGetPrompt(ACL_t * acl, int dirno);
extern NSAPI_PUBLIC char **aclGetRights(ACL_t * acl);
extern NSAPI_PUBLIC unsigned long aclGetRightsMask(ACContext_t * acc, char **rlist);
extern NSAPI_PUBLIC char * aclGetSignature(ACL_t * acl);
extern NSAPI_PUBLIC char **aclGetUsers(ACL_t * acl, int dirno, int clsno);
extern NSAPI_PUBLIC int aclDNSFilterStrings(char **list, DNSFilter_t * dnf);
extern NSAPI_PUBLIC int aclIPFilterStrings(char **list, IPFilter_t * ipf);
extern NSAPI_PUBLIC int aclIdsToNames(char **list,
			 USIList_t * uilptr, int uflag, Realm_t * rlm);
extern NSAPI_PUBLIC int aclMakeNew(ACContext_t * acc, char * aclsig, char * aclname,
		      char **rights, int flags, ACL_t **pacl);
extern NSAPI_PUBLIC int aclPutAllowDeny(NSErr_t * errp, ACL_t * acl,
			   int always, int allow, char **users, char **hosts);
extern NSAPI_PUBLIC int aclPutAuth(NSErr_t * errp, ACL_t * acl,
		      int always, int amethod, char * dbname, char * prompt);
extern NSAPI_PUBLIC char * aclSafeIdent(char * str);
extern NSAPI_PUBLIC int aclSetRights(ACL_t * acl, char **rights, int replace);
extern NSAPI_PUBLIC int accWriteFile(ACContext_t * acc, char * filename, int flags);
extern NSAPI_PUBLIC int aclStringGet(LEXStream_t * lst);
extern NSAPI_PUBLIC int aclStringOpen(NSErr_t * errp,
			 int slen, char * sptr, int flags, ACLFile_t **pacf);
extern NSAPI_PUBLIC int aclCheckUsers(NSErr_t * errp, char * dbpath, char * usernames,
			 char * groupnames, char ***uglist, char ***badulist,
			 char ***badglist);
extern NSAPI_PUBLIC int aclCheckHosts(NSErr_t * errp,
			 int hexpand, char * dnsspecs, char * ipspecs,
			 char ***hlist, char ***baddns, char ***badip);

#ifdef NOTDEF
extern int aclSetAuthMethod(ACL_t * acl, int dirno, char * amethod);
extern int aclSetDatabase(ACL_t * acl, int dirno, char * dbname);
extern int aclSetExecOptions(ACL_t * acl, char **options);
extern int aclSetHosts(ACL_t * acl, int dirno, char **hostlist);
extern int aclSetPrompt(ACL_t * acl, int dirno, char * prompt);
extern int aclSetUsers(ACL_t * acl, int dirno, char **userlist);
#endif /* NOTDEF */

NSPR_END_EXTERN_C

#endif /* __acladmin_h */

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
#ifndef __nsauth_h
#define __nsauth_h

/*
 * Description (nsauth.h)
 *
 *	This file defines types and interfaces which pertain to client
 *	authentication.  The key types are Realm_t, which describes a
 *	context for authentication, and ClAuth_t, which is used to
 *	pass authentication information about a particular client
 *	into and out of authentication interface functions.
 */

#ifdef CLIENT_AUTH

#include "ssl.h"

#if 0
/* Removed for new ns security */
#include "sec.h"		/* SECCertificate */
#endif
#include "cert.h"               /* CERTCertificate for new ns security bin */
#endif /* CLIENT_AUTH */

#include "usi.h"		/* identifier list support */
#include "attrec.h"		/* attribute record types */
#include "nserror.h"		/* error frame list support */
#include "nsautherr.h"		/* authentication error codes */

/* Define a scalar IP address value */
#ifndef __IPADDR_T_
#define __IPADDR_T_
typedef unsigned long IPAddr_t;
#endif /* __IPADDR_T_ */

/*
 * Description (UserObj_t)
 *
 *	This type defines the structure of a user object.  A user object
 *	contains information about a user which might be contained in
 *	an authentication database, including user name, password, user id,
 *	and group membership.
 */

typedef struct UserObj_s UserObj_t;
struct UserObj_s {
    NTS_t uo_name;		/* user account name */
    NTS_t uo_pwd;		/* encrypted password */
    USI_t uo_uid;		/* user id */
    USI_t uo_flags;		/* bit flags */
#define UOF_DBFLAGS	0x1f	/* mask for flags stored in DB file */
#define UOF_ERROR	0x20	/* error on last operation */
#define UOF_NEW		0x40	/* new user object */
#define UOF_MODIFIED	0x80	/* internal object modified */
#define UOF_DELPEND	0x100	/* delete pending */

    NTS_t uo_rname;		/* real user name (gecos string) */
    USIList_t uo_groups;	/* list of group ids containing user */
};

/*
 * Description (GroupObj_t)
 *
 *	This type defines the structure of a group object.  A group object
 *	contains information about a group which might be contained in
 *	an authentication database, including group name, group id, and
 *	relationships to other groups.
 */

typedef struct GroupObj_s GroupObj_t;
struct GroupObj_s {
    NTS_t go_name;		/* group name */
    USI_t go_gid;		/* group id */
    USI_t go_flags;		/* bit flags */
#define GOF_DBFLAGS	0x3f	/* mask for flags stored in DB file */
#define GOF_NEW		0x40	/* new group object */
#define GOF_MODIFIED	0x80	/* internal object modified */
#define GOF_DELPEND	0x100	/* delete pending */

    NTS_t go_desc;		/* group description */
    USIList_t go_users;		/* list of user members (uids) */
    USIList_t go_groups;	/* list of group members (gids) */
    USIList_t go_pgroups;	/* list of parent groups (gids) */
};

/*
 * Description (AuthIF_t)
 *
 *	This type describes a structure containing pointers to functions
 *	which provide a standard interface to an authentication database.
 *	The functions are described below.
 *
 *   Description (aif_close)
 *
 *	The referenced function closes an authentication database which
 *	was previously opened via the aif_open function.
 *
 *   Arguments:
 *
 *	authdb			- handle for database returned by aif_open
 *	flags			- close flags (unused - must be zero)
 *
 *
 *   Description (aif_findid)
 *
 *	The referenced function looks up a specified user or group id
 *	in a given authentication database.  Flags can be specified to
 *	search for only matching user ids, only matching group ids,
 *	or both.  The result value for a successful search indicates
 *	whether a matching user or group id was found, and a pointer to
 *	a user or group object is returned accordingly.
 *
 *   Arguments:
 *
 *	authdb			- handle for database returned by aif_open
 *	id			- user/group id value
 *	flags			- bit flags to control search
 *	rptr			- pointer to returned user or group object
 *				  pointer (may be null)
 *
 *   Returns:
 *
 *	If successful, the result value is greater than zero, and contains
 *	a subset of the search flags, indicating what was found, and a user
 *	or group object pointer is returned through 'rptr' if it is non-null.
 *	An unsuccessful search is indicated by a return value of zero.  An
 *	error is indicated by a negative return value (defined in
 *	nsautherr.h).
 *
 *
 *   Description (aif_findname)
 *
 *	The referenced function looks up a specified user or group name
 *	in a given authentication database.  Flags can be specified to
 *	search for only matching user names, only matching group names,
 *	or both.  The result value for a successful search indicates
 *	whether a matching user or group was found, and a pointer to a
 *	user or group object is returned accordingly.
 *
 *   Arguments:
 *
 *	authdb			- handle for database returned by aif_open
 *	name			- user/group name string pointer
 *	flags			- bit flags to control search
 *	rptr			- pointer to returned user or group object
 *				  pointer (may be null)
 *
 *   Returns:
 *
 *	If successful, the result value is greater than zero, and contains
 *	a subset of the search flags, indicating what was found, and a user
 *	or group object pointer is returned through 'rptr' if it is non-null.
 *	An unsuccessful search is indicated by a return value of zero.  An
 *	error is indicated by a negative return value (defined in
 *	nsautherr.h).
 *
 *
 *   Description (aif_idtoname)
 *
 *	The referenced function looks up a specified user or group id
 *	in a given authentication database, and returns the associated
 *	user or group name.  Flags can be specified to search for only
 *	matching user ids, only matching group ids, or both.  The result
 *	value for a successful search indicates whether a matching user
 *	or group id was found, and a pointer to the user or group name
 *	is returned accordingly.
 *
 *   Arguments:
 *
 *	authdb			- handle for database returned by aif_open
 *	id			- user/group id value
 *	flags			- bit flags to control search
 *	rptr			- pointer to returned user or group name
 *				  pointer (may be null)
 *
 *   Returns:
 *
 *	If successful, the result value is greater than zero, and contains
 *	a subset of the search flags, indicating what was found, and a user
 *	or group name pointer is returned through 'rptr' if it is non-null.
 *	An unsuccessful search is indicated by a return value of zero.  An
 *	error is indicated by a negative return value (defined in
 *	nsautherr.h).
 *
 *
 *   Description (aif_open)
 *
 *	The referenced function opens a named authentication database of
 *	the type supported by this interface.  The actual effect of the
 *	open function depends on the particular type of database, but a
 *	call to the aif_open function should generally be followed by a
 *	call to the aif_close function at some point.
 *
 *   Arguments:
 *
 *	adbname			- authentication database name string pointer
 *	flags			- open flags (definitions below)
 *	rptr			- pointer to returned handle for the database
 *
 *   Returns:
 *
 *	The return value is zero if the operation is successful, and a
 *	handle for the authentication database is returned through 'rptr'.
 *	An error is indicated by a negative return value (defined in
 *	nsautherr.h).
 */

typedef struct AuthIF_s AuthIF_t;
struct AuthIF_s {
    int (*aif_findid)(NSErr_t * errp,
		      void * authdb, USI_t id, int flags, void **rptr);
    int (*aif_findname)(NSErr_t * errp,
			void * authdb, char * name, int flags, void **rptr);
    int (*aif_idtoname)(NSErr_t * errp,
			void * authdb, USI_t id, int flags, char **rptr);
    int (*aif_open)(NSErr_t * errp, char * adbname, int flags, void **rptr);
    void (*aif_close)(void * authdb, int flags);
    int (*aif_addmember)(void **pmlist, char * name, int flags);
    int (*aif_ismember)(void * mlist, char * name, int flags);
};

/* Define flags for the aif_open function */
#define AIF_CREATE	0x1		/* new database (create it) */

/*
 * Define bits for flags and return value of aif_findid, aif_findid,
 * and aif_idtoname functions.
 */
#define AIF_NONE	0		/* no matching group or user name */
#define AIF_GROUP	0x1		/* matching group name/id found */
#define AIF_USER	0x2		/* matching user name/id found */

/*
 * Description (Realm_t)
 *
 *	This type defines a structure which represents an authentication
 *	realm.  Each realm has a unique name, which is accessed through
 *	a Symbol_t structure, which in turn references a Realm_t as the
 *	symbol value.  This structure specifies an authentication
 *	method and an authentication database.
 */

typedef struct Realm_s Realm_t;
struct Realm_s {
    int rlm_ameth;		/* authentication method type */
    char * rlm_dbname;		/* authentication database name */
    AuthIF_t * rlm_aif;		/* authentication interface pointer */
    void * rlm_authdb;		/* authentication database handle */
    char * rlm_prompt;		/* realm prompt string */
};

/* Define supported authentication method codes for rlm_ameth */
#define AUTH_METHOD_BASIC	1	/* basic authentication */
#define AUTH_METHOD_SSL		2	/* SSL client authentication */

/*
 * Description (ClAuth_t)
 *
 *	This type describes a structure containing information about a
 *	particular client.  It is used to pass information into and out
 *	of authentication support functions, as well as to other functions
 *	needing access to client authentication information.
 * FUTURE:
 *	- add client certificate pointer
 */

typedef struct ClAuth_s ClAuth_t;
struct ClAuth_s {
    Realm_t * cla_realm;	/* authentication realm pointer */
    IPAddr_t cla_ipaddr;	/* IP address */
    char * cla_dns;		/* DNS name string pointer */
    UserObj_t * cla_uoptr;	/* authenticated user object pointer */
    GroupObj_t * cla_goptr;	/* pointer to list of group objects */
#ifdef CLIENT_AUTH
#if 0
  /* Removed for new ns security  */
    SECCertificate * cla_cert;	/* certificate from SSL client auth */
#endif
    CERTCertificate * cla_cert;	/* certificate from SSL client auth */
#endif /* CLIENT_AUTH */
};

#endif /* __nsauth_h */

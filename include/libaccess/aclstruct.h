/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __aclstruct_h
#define __aclstruct_h

/*
 * Description (aclstruct.h)
 *
 *	This file defines types and data structures used to construct
 *	representations of Access Control Lists (ACLs) in memory.
 */

#include "base/systems.h"
#include "base/file.h"
#include "base/lexer.h"
#include "nsauth.h"		/* authentication types */
#include "symbols.h"		/* typed symbol support */
#include "ipfstruct.h"		/* IP address filter structures */
#include "dnfstruct.h"		/* DNS name filter structures */


NSPR_BEGIN_EXTERN_C

/* Forward type definitions */
typedef struct ACL_s ACL_t;

/*
 * Description (InetHost_t)
 *
 *	This type defines a structure which represents a list of Internet
 *	hosts by IP address and netmask, or by fully or partially
 *	qualified DNS name.
 */

typedef struct InetHost_s InetHost_t;
struct InetHost_s {
    IPFilter_t inh_ipf;			/* reference to IP filter */
    DNSFilter_t inh_dnf;		/* reference to DNS filter */
};

/*
 * Description (HostSpec_t)
 *
 *	This type describes a named list of hosts.
 */

typedef struct HostSpec_s HostSpec_t;
struct HostSpec_s {
    Symbol_t hs_sym;			/* symbol name, type ACLSYMHOST */
    InetHost_t hs_host;			/* host information */
};

/*
 * Description (UidUser_t)
 *
 *	This type represents a list of users and groups using unique
 *	integer identifiers.
 */

typedef struct UidUser_s UidUser_t;
struct UidUser_s {
    USIList_t uu_user;			/* list of user ids */
    USIList_t uu_group;			/* list of group ids */
};

/*
 * Description (UserSpec_t)
 *
 *	This type describes a named list of users and groups.
 */

typedef struct UserSpec_s UserSpec_t;
struct UserSpec_s {
    Symbol_t us_sym;			/* list name, type ACLSYMUSER */
    int us_flags;			/* bit flags */
#define ACL_USALL	0x1		/* any authenticated user */

    UidUser_t us_user;			/* user list structure */
};

/*
 * Description (ACClients_t)
 *
 *	This type defines the structure of action-specific information
 *	for access control directives with action codes ACD_ALLOW and
 *	ACD_DENY.  These directives specify access control constraints
 *	on users/groups and hosts.
 */

typedef struct ACClients_s ACClients_t;
struct ACClients_s {
    ACClients_t * cl_next;		/* list link */
    HostSpec_t * cl_host;		/* host specification pointer */
    UserSpec_t * cl_user;		/* user list pointer */
};

/*
 * Description (RealmSpec_t)
 *
 *	This type describes a named realm.
 */

typedef struct RealmSpec_s RealmSpec_t;
struct RealmSpec_s {
    Symbol_t rs_sym;			/* realm name, type ACLSYMREALM */
    Realm_t rs_realm;			/* realm information */
};

/*
 * Description (ACAuth_t)
 *
 *	This type defines the structure of action-specific information
 *	for an access control directive with action code ACD_AUTH,
 *	which specifies information about authentication requirements.
 */

typedef struct ACAuth_s ACAuth_t;
struct ACAuth_s {
    RealmSpec_t * au_realm;		/* pointer to realm information */
};

/*
 * Description (ACDirective_t)
 *
 *	This type defines a structure which represents an access control
 *	directive.  Each directive specifies an access control action
 *	to be taken during ACL evaluation.  The ACDirective_t structure
 *	begins an action-specific structure which contains the
 *	parameters for an action.
 */

typedef struct ACDirective_s ACDirective_t;
struct ACDirective_s {
    ACDirective_t * acd_next;		/* next directive in ACL */
    short acd_action;			/* directive action code */
    short acd_flags;			/* action modifier flags */

    /* Begin action-specific information */
    union {
	ACClients_t * acu_cl;		/* ACD_ALLOW, ACD_DENY */
	ACAuth_t acu_auth;		/* ACD_AUTH */
    } acd_u;
};

#define acd_cl		acd_u.acu_cl
#define acd_auth	acd_u.acu_auth

/* Define acd_action codes */
#define ACD_ALLOW	1		/* allow access */
#define ACD_DENY	2		/* deny access */
#define ACD_AUTH	3		/* specify authentication realm */
#define ACD_EXEC	4		/* execute (conditionally) */

/* Define acd_flags values */
#define ACD_ACTION	0xf		/* bits reserved for acd_action */
#define ACD_FORCE	0x10		/* force of action */
#define ACD_DEFAULT	0		/* default action */
#define ACD_ALWAYS	ACD_FORCE	/* immediate action */
#define ACD_EXALLOW	0x20		/* execute if allow */
#define ACD_EXDENY	0x40		/* execute if deny */
#define ACD_EXAUTH	0x80		/* execute if authenticate */

/*
 * Description (RightDef_t)
 *
 *	This type describes a named access right.  Each access right has
 *	an associated unique integer id.  A list of all access rights
 *	known in an ACL context is maintained, with its head in the
 *	ACContext_t structure.
 */

typedef struct RightDef_s RightDef_t;
struct RightDef_s {
    Symbol_t rd_sym;			/* right name, type ACLSYMRIGHT */
    RightDef_t * rd_next;		/* next on ACContext_t list */
    USI_t rd_id;			/* unique id */
};

/*
 * Description (RightSpec_t)
 *
 *	This type describes a named list of access rights.
 */

typedef struct RightSpec_s RightSpec_t;
struct RightSpec_s {
    Symbol_t rs_sym;			/* list name, type ACLSYMRDEF */
    USIList_t rs_list;			/* list of right ids */
};

/*
 * Description (ACContext_t)
 *
 *	This type defines a structure that defines a context for a set
 *	of Access Control Lists.  This includes references to an
 *	authentication database, if any, and a symbol table containing
 *	access right definitions.  It also serves as a list head for the
 *	ACLs which are defined in the specified context.
 */

typedef struct ACContext_s ACContext_t;
struct ACContext_s {
    void * acc_stp;			/* symbol table handle */
    ACL_t * acc_acls;			/* list of ACLs */
    RightDef_t * acc_rights;		/* list of access right definitions */
    int acc_refcnt;			/* reference count */
};

/*
 * Description (ACL_t)
 *
 *	This type defines the structure that represents an Access Control
 *	List (ACL).  An ACL has a user-assigned name and an internally
 *	assigned identifier (which is an index in an object directory).
 *	It references a list of access rights which are to be allowed or
 *	denied, according to the ACL specifications.  It references an
 *	ordered list of ACL directives, which specify who has and who does
 *	not have the associated access rights.
 */

struct ACL_s {
    Symbol_t acl_sym;			/* ACL name, type ACLSYMACL */
    ACL_t * acl_next;			/* next ACL on a list */
    ACContext_t * acl_acc;		/* context for this ACL */
    USI_t acl_id;			/* id of this ACL */
    int acl_refcnt;			/* reference count */
    RightSpec_t * acl_rights;		/* access rights list */
    ACDirective_t * acl_dirf;		/* first directive pointer */
    ACDirective_t * acl_dirl;		/* last directive pointer */
};

/* Define symbol type codes */
#define ACLSYMACL	0		/* ACL */
#define ACLSYMRIGHT	1		/* access right */
#define ACLSYMRDEF	2		/* access rights list */
#define ACLSYMREALM	3		/* realm name */
#define ACLSYMHOST	4		/* host specifications */
#define ACLSYMUSER	5		/* user/group list */

/*
 * Description (ACLFile_t)
 *
 *	This type describes a structure containing information about
 *	an open ACL description file.
 */

typedef struct ACLFile_s ACLFile_t;
struct ACLFile_s {
    ACLFile_t * acf_next;		/* list link */
    char * acf_filename;		/* pointer to filename string */
    LEXStream_t * acf_lst;		/* LEX stream handle */
    SYS_FILE acf_fd;			/* file descriptor */
    int acf_flags;			/* bit flags (unused) */
    int acf_lineno;			/* current line number */
    void * acf_token;			/* LEX token handle */
    int acf_ttype;			/* current token type */
};

NSPR_END_EXTERN_C

#endif /* __aclstruct_h */

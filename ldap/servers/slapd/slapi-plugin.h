/* BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK */

/*! \file slapi-plugin.h
 *  \brief Public Directory Server plugin interface.
 *
 *  The SLAPI plugin interface allows complex plugins to be created
 *  for Directory Server.
 */


#ifndef SLAPIPLUGIN_H_
#define SLAPIPLUGIN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Provides our int types and platform specific requirements. */
#include "slapi_pal.h"
#include "prtypes.h"
#include "ldap.h"
#include "prprf.h"
#include "nspr.h"
#include <syslog.h>

#ifdef __GNUC__
    #define __ATTRIBUTE__(x) __attribute__(x)
#else
    #define __ATTRIBUTE__(x)
#endif

NSPR_API(PRUint32)
PR_snprintf(char *out, PRUint32 outlen, const char *fmt, ...) __ATTRIBUTE__((format(printf, 3, 4)));
NSPR_API(char *)
PR_smprintf(const char *fmt, ...) __ATTRIBUTE__((format(printf, 1, 2)));
NSPR_API(char *)
PR_sprintf_append(char *last, const char *fmt, ...) __ATTRIBUTE__((format(printf, 2, 3)));
NSPR_API(PRUint32)
PR_fprintf(struct PRFileDesc *fd, const char *fmt, ...) __ATTRIBUTE__((format(printf, 2, 3)));

/* OpenLDAP uses unsigned long for ber_tag_t and ber_len_t */
/* use this macro for printf statements for ber_tag_t and ber_len_t */
#define BERTAG_T "lu"
#define BERLEN_T "lu"

#define DEFINETOSTR(xxx) #xxx
#define STRINGIFYDEFINE(xxx) DEFINETOSTR(xxx)

/* Common check on berval before accessing the contents. */
/* bv is a struct berval *bv */
#define BV_HAS_DATA(bv) ((bv != NULL) && (bv->bv_len > 0) && (bv->bv_val != NULL))

/*
 * The slapi_attr_get_flags() routine returns a bitmap that contains one or
 * more of these values.
 *
 * Note that the flag values 0x0010, 0x0020, 0x4000, and 0x8000 are reserved.
 */
/**
 * Flag indicating that an attribtue is single-valued.
 *
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_SINGLE 0x0001 /* single-valued attribute */

/**
 * Flag indicating than an attribute is operational.
 *
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_OPATTR 0x0002 /* operational attribute */

/**
 * Flag indicating than an attribute is read-only.
 *
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_READONLY 0x0004 /* read from shipped config file */

/**
 * Flag indicating than an attribute is read-only.
 *
 *  This is an alias for #SLAPI_ATTR_FLAG_READONLY.
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_STD_ATTR SLAPI_ATTR_FLAG_READONLY /* alias for read only */

/**
 * Flag indicating than an attribute is obsolete.
 *
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_OBSOLETE 0x0040 /* an outdated definition */

/**
 * Flag indicating that an attribute is collective.
 *
 * \warning Collective attributes are not supported, so this
 *          flag has no effect.
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_COLLECTIVE 0x0080 /* collective (not supported) */

/**
 * Flag indicating that an attribute is not modifiable over LDAP.
 *
 * \see slapi_attr_flag_is_set()
 * \see slapi_attr_set_flags()
 */
#define SLAPI_ATTR_FLAG_NOUSERMOD 0x0100 /* can't be modified over LDAP */

/**
 * Flag to indicate that the attribute value is normalized.
 *
 * \see slapi_value_set_flags()
 * \see slapi_values_set_flags()
 */
/* the attr value is normalized */
#define SLAPI_ATTR_FLAG_NORMALIZED \
    (SLAPI_ATTR_FLAG_NORMALIZED_CES | SLAPI_ATTR_FLAG_NORMALIZED_CIS)
#define SLAPI_ATTR_FLAG_NORMALIZED_CES 0x0200 /* the attr value is normalized, \
                                                 but not case-normalized.      \
                                                 Used for DN. */
#define SLAPI_ATTR_FLAG_NORMALIZED_CIS 0x0400 /* the attr value is normalized \
                                                 including case.              \
                                                 Used for DN. */

/**
 * Flag to indicate that the attribute value is not exposed if specified.
 *
 * \see slapi_value_set_flags()
 * \see slapi_values_set_flags()
 */
#define SLAPI_ATTR_FLAG_NOEXPOSE 0x0800 /* the attr value is not exposed */

/**
 * Flag to indicate that the attribute value is LDAP_SCHEMA_DISTRIBUTED_OPERATION
 * If this is set, SLAPI_ATTR_FLAG_OPATTR must also be set
 *
 * \see slapi_value_set_flags()
 * \see slapi_values_set_flags()
*/
#define SLAPI_ATTR_FLAG_DISTRIBUTED_OPERATION 0x1000 /* USAGE distributedOperation */

/**
 * Flag to indicate that the attribute value is LDAP_SCHEMA_DSA_OPERATION
 * If this is set, SLAPI_ATTR_FLAG_OPATTR must also be set
 *
 * \see slapi_value_set_flags()
 * \see slapi_values_set_flags()
 */
#define SLAPI_ATTR_FLAG_DSA_OPERATION 0x2000 /* USAGE dSAOperation */

/* operation flags */
#define SLAPI_OP_FLAG_INTERNAL         0x0000020  /* An operation generated by the core server or a plugin. */
#define SLAPI_OP_FLAG_NEVER_CHAIN      0x0000800  /* Do not chain the operation */
#define SLAPI_OP_FLAG_TOMBSTONE_ENTRY  0x0001000  /* Operation is done on a tombstone entry */
#define SLAPI_OP_FLAG_NO_ACCESS_CHECK  0x0010000  /* Do not check for access control - bypass them */
#define SLAPI_OP_FLAG_BYPASS_REFERRALS 0x0040000  /* Useful for performing internal operations on read-only replica */
#define SLAPI_OP_FLAG_NEVER_CACHE      0x0200000  /* added entry should not be kept in cache */
#define SLAPI_OP_FLAG_IGNORE_UNINDEXED 0x0800000  /* Do not log unindexed search */
#define SLAPI_OP_FLAG_FIXUP            0x1000000  /* Fix up operation, bypass restrictions */

#define SLAPI_OC_FLAG_REQUIRED 0x0001
#define SLAPI_OC_FLAG_ALLOWED  0x0002

/*
 * access control levels
 */
#define SLAPI_ACL_COMPARE 0x01
#define SLAPI_ACL_SEARCH  0x02
#define SLAPI_ACL_READ    0x04
#define SLAPI_ACL_WRITE   0x08
#define SLAPI_ACL_DELETE  0x10
#define SLAPI_ACL_ADD     0x20
#define SLAPI_ACL_SELF    0x40
#define SLAPI_ACL_PROXY   0x80
/* Values 0x200 and 0x400 are booked (acl.h) by
 * ACLPB_SLAPI_ACL_WRITE_ADD
 * ACLPB_SLAPI_ACL_WRITE_DEL
 */
#define SLAPI_ACL_MODDN 0x0800
#define SLAPI_ACL_ALL   0x087f


/*
 * filter types
 * openldap defines these, except the 'extended' is called 'ext' so just redefine
 */
#define LDAP_FILTER_EXTENDED LDAP_FILTER_EXT

#ifndef LBER_END_OF_SEQORSET
#define LBER_END_OF_SEQORSET ((ber_tag_t)-2) /* 0xfffffffeU */
#endif

#ifndef LDAP_CHANGETYPE_ADD
#ifdef LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_ADD
#define LDAP_CHANGETYPE_ADD LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_ADD
#else
#define LDAP_CHANGETYPE_ADD 1
#endif
#endif
#ifndef LDAP_CHANGETYPE_DELETE
#ifdef LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_DELETE
#define LDAP_CHANGETYPE_DELETE LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_DELETE
#else
#define LDAP_CHANGETYPE_DELETE 2
#endif
#endif
#ifndef LDAP_CHANGETYPE_MODIFY
#ifdef LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_MODIFY
#define LDAP_CHANGETYPE_MODIFY LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_MODIFY
#else
#define LDAP_CHANGETYPE_MODIFY 4
#endif
#endif
#ifndef LDAP_CHANGETYPE_MODDN
#ifdef LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_RENAME
#define LDAP_CHANGETYPE_MODDN LDAP_CONTROL_PERSSIT_ENTRY_CHANGE_RENAME
#else
#define LDAP_CHANGETYPE_MODDN 8
#endif
#endif
#ifndef LDAP_CHANGETYPE_ANY
#define LDAP_CHANGETYPE_ANY (1 | 2 | 4 | 8)
#endif

#ifndef LDAP_CONTROL_PERSISTENTSEARCH
#ifdef LDAP_CONTROL_PERSIST_REQUEST
#define LDAP_CONTROL_PERSISTENTSEARCH LDAP_CONTROL_PERSIST_REQUEST
#else
#define LDAP_CONTROL_PERSISTENTSEARCH "2.16.840.1.113730.3.4.3"
#endif
#endif
#ifndef LDAP_CONTROL_ENTRYCHANGE
#ifdef LDAP_CONTROL_PERSIST_ENTRY_CHANGE_NOTICE
#define LDAP_CONTROL_ENTRYCHANGE LDAP_CONTROL_PERSIST_ENTRY_CHANGE_NOTICE
#else
#define LDAP_CONTROL_ENTRYCHANGE "2.16.840.1.113730.3.4.7"
#endif
#endif

#ifndef LDAP_CONTROL_PWEXPIRED
#define LDAP_CONTROL_PWEXPIRED "2.16.840.1.113730.3.4.4"
#endif
#ifndef LDAP_CONTROL_PWEXPIRING
#define LDAP_CONTROL_PWEXPIRING "2.16.840.1.113730.3.4.5"
#endif
#ifndef LDAP_X_CONTROL_PWPOLICY_REQUEST
#ifdef LDAP_CONTROL_PASSWORDPOLICYREQUEST
#define LDAP_X_CONTROL_PWPOLICY_REQUEST LDAP_CONTROL_PASSWORDPOLICYREQUEST
#else
#define LDAP_X_CONTROL_PWPOLICY_REQUEST "1.3.6.1.4.1.42.2.27.8.5.1"
#endif
#endif
#ifndef LDAP_X_CONTROL_PWPOLICY_RESPONSE
#ifdef LDAP_CONTROL_PASSWORDPOLICYRESPONSE
#define LDAP_X_CONTROL_PWPOLICY_RESPONSE LDAP_CONTROL_PASSWORDPOLICYRESPONSE
#else
#define LDAP_X_CONTROL_PWPOLICY_RESPONSE "1.3.6.1.4.1.42.2.27.8.5.1"
#endif
#endif

#ifndef LDAP_CONTROL_PROXYAUTH
#define LDAP_CONTROL_PROXYAUTH "2.16.840.1.113730.3.4.12" /* version 1 */
#endif
#ifndef LDAP_CONTROL_PROXIEDAUTH
#ifdef LDAP_CONTROL_PROXY_AUTHZ
#define LDAP_CONTROL_PROXIEDAUTH LDAP_CONTROL_PROXY_AUTHZ
#else
#define LDAP_CONTROL_PROXIEDAUTH "2.16.840.1.113730.3.4.18" /* version 2 */
#endif
#endif

#ifndef LDAP_CONTROL_AUTH_REQUEST
#define LDAP_CONTROL_AUTH_REQUEST "2.16.840.1.113730.3.4.16"
#endif

/* Read Entry Controls - PRE & POST */
#ifndef LDAP_CONTROL_PRE_READ_ENTRY
#define LDAP_CONTROL_PRE_READ_ENTRY "1.3.6.1.1.13.1"
#endif
#ifndef LDAP_CONTROL_POST_READ_ENTRY
#define LDAP_CONTROL_POST_READ_ENTRY "1.3.6.1.1.13.2"
#endif

#ifndef LDAP_SORT_CONTROL_MISSING
#define LDAP_SORT_CONTROL_MISSING 0x3C /* 60 (server side sort extn) */
#endif

#ifndef LDAP_INDEX_RANGE_ERROR
#define LDAP_INDEX_RANGE_ERROR 0x3D /* 61 (VLV extn) */
#endif

/* openldap does not use this */
#ifndef LBER_OVERFLOW
#define LBER_OVERFLOW ((ber_tag_t)-3) /* 0xfffffffdU */
#endif

#ifndef LDAP_SYNC_OID
/* LDAP Content Synchronization Operation -- RFC 4533 */
#define LDAP_SYNC_OID                         "1.3.6.1.4.1.4203.1.9.1"
#define LDAP_CONTROL_SYNC LDAP_SYNC_OID       ".1"
#define LDAP_CONTROL_SYNC_STATE LDAP_SYNC_OID ".2"
#define LDAP_CONTROL_SYNC_DONE LDAP_SYNC_OID  ".3"
#define LDAP_SYNC_INFO LDAP_SYNC_OID          ".4"
#define LDAP_TAG_SYNC_NEW_COOKIE              ((ber_tag_t)0x80U)
#define LDAP_TAG_SYNC_REFRESH_DELETE          ((ber_tag_t)0xa1U)
#define LDAP_TAG_SYNC_REFRESH_PRESENT         ((ber_tag_t)0xa2U)
#define LDAP_TAG_SYNC_ID_SET                  ((ber_tag_t)0xa3U)
#define LDAP_SYNC_NONE                        0x00
#define LDAP_SYNC_REFRESH_ONLY                0x01
#define LDAP_SYNC_RESERVED                    0x02
#define LDAP_SYNC_REFRESH_AND_PERSIST         0x03
#define LDAP_SYNC_REFRESH_PRESENTS            0
#define LDAP_SYNC_REFRESH_DELETES             1
#define LDAP_TAG_SYNC_COOKIE                  ((ber_tag_t)0x04U)
#define LDAP_TAG_REFRESHDELETES               ((ber_tag_t)0x01U)
#define LDAP_TAG_REFRESHDONE                  ((ber_tag_t)0x01U)
#define LDAP_TAG_RELOAD_HINT                  ((ber_tag_t)0x01U)
#define LDAP_SYNC_PRESENT                     0
#define LDAP_SYNC_ADD                         1
#define LDAP_SYNC_MODIFY                      2
#define LDAP_SYNC_DELETE                      3
#define LDAP_SYNC_NEW_COOKIE                  4
#endif

#ifndef LDAP_REQ_BIND
#define LDAP_REQ_BIND     ((ber_tag_t)0x60U)   /* application + constructed */
#define LDAP_REQ_UNBIND   ((ber_tag_t)0x42U) /* application + primitive   */
#define LDAP_REQ_SEARCH   ((ber_tag_t)0x63U) /* application + constructed */
#define LDAP_REQ_MODIFY   ((ber_tag_t)0x66U) /* application + constructed */
#define LDAP_REQ_ADD      ((ber_tag_t)0x68U)    /* application + constructed */
#define LDAP_REQ_DELETE   ((ber_tag_t)0x4aU) /* application + primitive   */
#define LDAP_REQ_MODDN    ((ber_tag_t)0x6cU)  /* application + constructed */
#define LDAP_REQ_MODRDN   LDAP_REQ_MODDN
#define LDAP_REQ_RENAME   LDAP_REQ_MODDN
#define LDAP_REQ_COMPARE  ((ber_tag_t)0x6eU)  /* application + constructed */
#define LDAP_REQ_ABANDON  ((ber_tag_t)0x50U)  /* application + primitive   */
#define LDAP_REQ_EXTENDED ((ber_tag_t)0x77U) /* application + constructed */
#endif

#ifndef LDAP_SYNC_PRESENT
#define LDAP_SYNC_PRESENT    0
#define LDAP_SYNC_ADD        1
#define LDAP_SYNC_MODIFY     2
#define LDAP_SYNC_DELETE     3
#define LDAP_SYNC_NEW_COOKIE 4
#endif

/*
 * Sequential access types
 */
#define SLAPI_SEQ_FIRST 1
#define SLAPI_SEQ_LAST  2
#define SLAPI_SEQ_PREV  3
#define SLAPI_SEQ_NEXT  4


/*
 * return codes from a backend API call
 */
#define SLAPI_FAIL_GENERAL  -1
#define SLAPI_FAIL_DISKFULL -2


/*
 * return codes used by BIND functions
 */
#define SLAPI_BIND_SUCCESS    0  /* front end will send result */
                                 /* 1 is reserved */
#define SLAPI_BIND_FAIL       2  /* back end should send result */
#define SLAPI_BIND_ANONYMOUS  3  /* front end will send result */
#define SLAPI_BIND_REFERRAL   4  /* caller should send result */
#define SLAPI_BIND_NO_BACKEND 5  /* caller should send result */


/* commonly used attributes names */
#define SLAPI_ATTR_UNIQUEID              "nsuniqueid"
#define SLAPI_ATTR_OBJECTCLASS           "objectclass"
#define SLAPI_ATTR_VALUE_TOMBSTONE       "nsTombstone"
#define SLAPI_ATTR_TOMBSTONE_CSN         "nsTombstoneCSN"
#define SLAPI_ATTR_VALUE_PARENT_UNIQUEID "nsParentUniqueID"
#define SLAPI_ATTR_VALUE_SUBENTRY        "ldapsubentry"
#define SLAPI_ATTR_NSCP_ENTRYDN          "nscpEntryDN"
#define SLAPI_ATTR_ENTRYUSN              "entryusn"
#define SLAPI_ATTR_ENTRYDN               "entrydn"
#define SLAPI_ATTR_DS_ENTRYDN            "dsEntryDN"
#define SLAPI_ATTR_DN                    "dn"
#define SLAPI_ATTR_RDN                   "rdn"
#define SLAPI_ATTR_PARENTID              "parentid"
#define SLAPI_ATTR_ENTRYUUID             "entryuuid"
#define SLAPI_ATTR_UNIQUEID_LENGTH              10
#define SLAPI_ATTR_OBJECTCLASS_LENGTH           11
#define SLAPI_ATTR_VALUE_TOMBSTONE_LENGTH       11
#define SLAPI_ATTR_TOMBSTONE_CSN_LENGTH         14
#define SLAPI_ATTR_VALUE_PARENT_UNIQUEID_LENGTH 16
#define SLAPI_ATTR_VALUE_SUBENTRY_LENGTH        12
#define SLAPI_ATTR_NSCP_ENTRYDN_LENGTH          11
#define SLAPI_ATTR_ENTRYUSN_LENGTH               8
#define SLAPI_ATTR_ENTRYDN_LENGTH                7
#define SLAPI_ATTR_DN_LENGTH                     2
#define SLAPI_ATTR_RDN_LENGTH                    3
#define SLAPI_ATTR_PARENTID_LENGTH               8

/* plugin shared config area */
#define SLAPI_PLUGIN_SHARED_CONFIG_AREA "nsslapd-pluginConfigArea"


/* opaque structures */
/**
 * Contains name-value pairs, known as parameter blocks, that you can get or set for
 * each LDAP operation.
 *
 * #Slapi_PBlock contains name-value pairs that you can use to retrieve information
 * from the server and set information to be used by the server.
 *
 * For most types of plug-in functions, the server passes in a #Slapi_PBlock
 * structure that typically includes data relevant to the operation being processed.
 * You can get the value of a parameter by calling the slapi_pblock_get() function.
 *
 * For example, when the plug-in function for an LDAP bind operation is called, the
 * server puts the DN and credentials in the #SLAPI_BIND_TARGET and
 * #SLAPI_BIND_CREDENTIALS parameters of the Slapi_PBlock structure. You can
 * call slapi_pblock_get() to get the DN and credentials of the client requesting
 * authentication.
 *
 * For plug-in initialization functions, you can use the #Slapi_PBlock structure to
 * pass information to the server, such as the description of your plug-in and the
 * names of your plug-in functions. You can set the value of a parameter by calling
 * the slapi_pblock_set() function.
 *
 * For example, in order to register a pre-operation bind plug-in function, you need to
 * call slapi_pblock_set() to set the version number, description, and name of the
 * plug-in function as the #SLAPI_PLUGIN_VERSION, #SLAPI_PLUGIN_DESCRIPTION,
 * and #SLAPI_PLUGIN_PRE_BIND_FN parameters.
 *
 * The available parameters that you can use depends on the type of plug-in function
 * you are writing.
 */
// Shadow pointer for pblock
struct slapi_pblock;

typedef struct slapi_pblock Slapi_PBlock;

/**
 * Represents an entry in the directory.
 *
 * #Slapi_Entry is the data type for an opaque structure that represents an entry in
 * the directory. In certain cases, your server plug-in may need to work with an entry
 * in the directory.
 */
typedef struct slapi_entry Slapi_Entry;

/**
 * Represents an attribute in an entry.
 *
 * #Slapi_Attr is the data type for an opaque structure that represents an attribute
 * in a directory entry. In certain cases, your server plug-in may need to work with
 * an entry’s attributes.
 */
typedef struct slapi_attr Slapi_Attr;

/**
 * Represents the value of the attribute in a directory entry.
 *
 * #Slapi_Value is the data type for an opaque structure that represents the value of
 * an attribute in a directory entry.
 */
typedef struct slapi_value Slapi_Value;

/**
 * Represents a set of Slapi_Value (or a list of Slapi_Value).
 *
 * #Slapi_ValueSet is the data type for an opaque structure that represents set of
 * #Slapi_Value (or a list of #Slapi_Value).
 */
typedef struct slapi_value_set Slapi_ValueSet;

/**
 * Represents a search filter.
 *
 * #Slapi_Filter is the data type for an opaque structure that represents an search
 * filter.
 */
typedef struct slapi_filter Slapi_Filter;

/**
 * Represents a backend operation in the server plug-in.
 *
 * #Slapi_Backend is the data type for an opaque structure that represents a backend
 * operation.
 */
typedef struct backend Slapi_Backend;

/**
 * Represents password scheme information
 */
typedef struct pw_scheme PWScheme;

/**
 * Represents the unique identifier of a directory entry.
 *
 * #Slapi_UniqueID is the data type for an opaque structure that represents the
 * unique identifier of a directory entry. All directory entries contain a unique
 * identifier. Unlike the distinguished name (DN), the unique identifier of an entry
 * never changes, providing a good way to refer unambiguously to an entry in a
 * distributed/replicated environment.
 */
typedef struct guid_t_ Slapi_UniqueID;

/**
 * Represents an operation pending from an LDAP client.
 *
 * #Slapi_Operation is the data type for an opaque structure that represents an
 * operation pending from an LDAP client.
 */
typedef struct op Slapi_Operation;

/**
 * Represents a connection.
 *
 * #Slapi_Connection is the data type for an opaque structure that represents a
 * connection.
 */
typedef struct conn Slapi_Connection;

/**
 * Represents a distinguished name in a directory entry.
 *
 * #Slapi_DN is the data type for an opaque structure that represents a distinguished
 * name in the server plug-in.
 */
typedef struct slapi_dn Slapi_DN;

/**
 * Represents a relative distinguished name in a directory entry.
 *
 * #Slapi_RDN is the data type for an opaque structure that represents a relative
 * distinguished name in the server plug-in.
 */
typedef struct slapi_rdn Slapi_RDN;

/**
 * Represents a single LDAP modification to a directory entry.
 *
 * #Slapi_Mod is the data type for an opaque structure that represents LDAPMod
 * modifications to an attribute in a directory entry.
 */
typedef struct slapi_mod Slapi_Mod;

/**
 * Represents two or more LDAP modifications to a directory entry
 *
 * #Slapi_Mods is the data type for an opaque structure that represents LDAPMod
 * manipulations that can be made to a directory entry.
 */
typedef struct slapi_mods Slapi_Mods;

/**
 * Represents a the component ID in a directory entry.
 *
 * #Slapi_ComponentId is the data type for an opaque structure that represents the
 * component ID in a directory entry.
 */
typedef struct slapi_componentid Slapi_ComponentId;

/**
 * Represents an integral counter.
 *
 * Provides 64-bit integers with support for atomic operations, even on 32-bit
 * systems.  This lets your plug-in have global integers that can be updated by
 * multiple worker threads in a thread-safe manner.
 *
 * The #Slapi_Counter structure is a wrapper around the actual counter value
 */
typedef struct slapi_counter Slapi_Counter;

/* Online tasks interface (to support import, export, etc) */
#define SLAPI_TASK_PUBLIC 1 /* tell old plugins that the task api is now public */

/**
 * An opaque structure that represents a task that has been initiated.
 *
 * Common Directory Server tasks, including importing, exporting, and indexing
 * databases, can be initiated through a special task configuration entry in
 * cn=tasks,cn=config. These task operations are managed using the #Slapi_Task
 * structure.
 */
typedef struct slapi_task Slapi_Task;

typedef struct passwordpolicyarray Slapi_PWPolicy;

/**
 * Defines a callback used specifically by Slapi_Task structure cancel and
 * destructor functions.
 *
 * \param task The task that is being cancelled or destroyed.
 */
typedef void (*TaskCallbackFn)(Slapi_Task *task);

/*
 * The default thread stacksize for nspr21 is 64k).
 * For OSF, we require a larger stacksize as actual storage allocation is
 * higher i.e pointers are allocated 8 bytes but lower 4 bytes are used.
 * The value 0 means use the default stacksize.
 *
 * HP-UX on PA-RISC uses 64KB default stack size.  Just set to 256K for all
 * HP-UX.
 */
#if (defined(hpux))
#define SLAPD_DEFAULT_THREAD_STACKSIZE 262144L
#else
/* All other platforms use the default stack size */
#define SLAPD_DEFAULT_THREAD_STACKSIZE 0
#endif


/*---------------------------
 * parameter block routines
 *--------------------------*/
/**
 * Creates a new parameter block.
 *
 * \return This function returns a pointer to the new parameter block.
 * \warning The pblock pointer allocated with this function must always be freed by
 *          slapi_pblock_destroy(). The use of other memory deallocators (for example,
 *          <tt>free()</tt>) is not supported and may lead to crashes or memory leaks.
 * \see slapi_pblock_destroy()
 */
Slapi_PBlock *slapi_pblock_new(void) __ATTRIBUTE__((returns_nonnull)); /* allocate and initialize */

/**
 * Initializes an existing parameter block for re-use.
 *
 * \param pb The parameter block to initialize.
 * \warning The parameter block that you wish to free must have been created using
 *          slapi_pblock_new().  When you are finished with the parameter block, you
 *          must free it using the slapi_pblock_destroy() function.
 *
 * \warning Note that search results will not be freed from the parameter block by
 *          slapi_pblock_init(). You must free any internal  search results with the
 *          slapi_free_search_results_internal() function prior to calling
 *          slapi_pblock_init(), otherwise the search results will be leaked.
 * \see slapi_pblock_new()
 * \see slapi_pblock_destroy()
 * \see slapi_free_search_results_internal()
 */
void slapi_pblock_init(Slapi_PBlock *pb); /* clear out for re-use */

/**
 * Gets the value of a name-value pair from a parameter block.
 *
 * \param pb Parameter block.
 * \param arg ID of the name-value pair that you want to get.
 * \param value Pointer to the value retrieved from the parameter block.
 * \return \c 0 if successful.
 * \return \c -1 if an error occurs (for example, if an invalid ID is specified).
 * \todo Document valid values for the ID.
 * \warning The <tt>void *value</tt> argument should always be a pointer to the
 *          type of value you are retrieving:
 * \code
 *     int connid = 0;
 *     ...
 *     retval = slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
 * \endcode
 *
 * \warning #SLAPI_CONN_ID is an integer value, so you will pass in a pointer
 *          to/address of an integer to get the value. Similarly, for a
 *          <tt>char *</tt> value (a string), pass in a pointer to/address of the value.
 *          For example:
 * \code
 *     char *binddn = NULL;
 *     ...
 *     retval = slapi_pblock_get(pb, SLAPI_CONN_DN, &binddn);
 * \endcode
 *
 * \warning With certain compilers on some platforms, you may have to cast the
 *          value to <tt>(void *)</tt>.
 *
 * \warning We recommend that you set the value to \c 0 or \c NULL before calling
 *          slapi_pblock_get() to avoid reading from uninitialized memory, in
 *          case the call to slapi_pblock_get() fails.
 *
 * \warning In most instances, the caller should not free the returned value.
 *          The value will usually be freed internally or through the call to
 *          slapi_pblock_destroy(). The exception is if the value is explicitly
 *          set by the caller through slapi_pblock_set(). In this case, the caller
 *          is responsible for memory management. If the value is freed, it is
 *          strongly recommended that the free is followed by a call to
 *          slapi_pblock_set() with a value of \c NULL. For example:
 * \code
 *     char *someparam = NULL;
 *     ...
 *     someparam = slapi_ch_strdup(somestring);
 *     slapi_pblock_set(pb, SOME_PARAM, someparam);
 *     someparam = NULL;
 *     ...
 *     slapi_pblock_get(pb, SOME_PARAM, &someparam);
 *     slapi_pblock_set(pb, SOME_PARAM, NULL);
 *     slapi_ch_free_string(&someparam);
 *     ...
 * \endcode
 *
 * \warning Some internal functions may change the value passed in, so it is
 *          recommended to use slapi_pblock_get() to retrieve the value again,
 *          rather than relying on a potential dangling pointer. This is shown
 *          in the example above, which sets someparam to \c NULL after setting
 *          it in the pblock.
 *
 * \see slapi_pblock_destroy()
 * \see slapi_pblock_set()
 */
__ATTRIBUTE__((access (write_only, 3))) int slapi_pblock_get(Slapi_PBlock *pb, int arg, void *value);

/**
 * Sets the value of a name-value pair in a parameter block.
 *
 * \param pb Parameter block.
 * \param arg ID of the name-value pair that you want to get.
 * \param value Pointer to the value you want to set in the parameter block.
 * \return \c 0 if successful.
 * \return \c -1 if an error occurs (for example, if an invalid ID is specified).
 * \warning The value to be passed in must always be a pointer, even for integer
 *          arguments. For example, if you wanted to do a search with the
 *          \c ManageDSAIT control:
 * \code
 *     int managedsait = 1;
 *     ...
 *     slapi_pblock_set(pb, SLAPI_MANAGEDSAIT, &managedsait);
 * \endcode
 *
 * \warning A call similar to the following example will cause a crash:
 * \code
 *     slapi_pblock_set(pb, SLAPI_MANAGEDSAIT, 1);
 * \endcode
 *
 * \warning However, for values which are already pointers, (<tt>char * string</tt>,
 *          <tt>char **arrays</tt>, <tt>#Slapi_Backend *</tt>, etc.), you can pass
 *          in the value directly. For example:
 * \code
 *     Slapi_DN *target_sdn = slapi_sdn_dup(some_sdn);
 *     slapi_pblock_set(pb, SLAPI_TARGET_SDN, target_sdn);
 * \endcode
 *
 * \warning or
 * \code
 *     slapi_pblock_set(pb, SLAPI_TARGET_SDN, NULL);
 * \endcode
 *
 * \warning With some compilers, you will have to cast the value argument to
 *          <tt>(void *)</tt>. If the caller allocates the memory passed in, the
 *          caller is responsible for freeing that memory. Also, it is recommended
 *          to use slapi_pblock_get() to retrieve the value to free, rather than
 *          relying on a potentially dangling pointer. See the slapi_pblock_get()
 *          example for more details.
 *
 * \warning When setting parameters to register a plug-in, the plug-in type must
 *          always be set first, since many of the plug-in parameters depend on
 *          the type. For example, set the #SLAPI_PLUGIN_TYPE to extended
 *          operation before setting the list of extended operation OIDs for
 *          the plug-in.
 *
 * \see slapi_pblock_get()
 */
__ATTRIBUTE__((access (read_only, 3))) int slapi_pblock_set(Slapi_PBlock *pb, int arg, void *value);

/**
 * Frees the specified parameter block from memory.
 *
 * \param pb Parameter block you want to free.
 * \warning The parameter block that you wish to free must have been created
 *          using slapi_pblock_new(). Use of this function with parameter
 *          blocks allocated on the stack (for example, <tt>#Slapi_PBlock pb;</tt>)
 *          or using another memory allocator is not supported and may lead to
 *          memory errors and memory leaks. For example:
 * \code
 *     Slapi_PBlock *pb = malloc(sizeof(Slapi_PBlock));
 * \endcode
 *
 * \warning After calling this function, you should set the parameter block
 *          pointer to \c NULL to avoid reusing freed memory in your function
 *          context, as in the following:
 * \code
 *     slapi_pblock_destroy(pb);
 *     pb =NULL;
 * \endcode
 *
 * \warning If you reuse the pointer in this way, it makes it easier to
 *          identify a Segmentation Fault, rather than using some difficult
 *          method to detect memory leaks or other abnormal behavior.
 *
 * \warning It is safe to call this function with a \c NULL pointer. For
 *          example:
 * \code
 *     Slapi_PBlock *pb = NULL;
 *     slapi_pblock_destroy(pb);
 * \endcode
 *
 * \warning This saves the trouble of checking for \c NULL before calling
 *          slapi_pblock_destroy().
 *
 * \see slapi_pblock_new()
 */
void slapi_pblock_destroy(Slapi_PBlock *pb);


/*----------------
 * entry routines
 *---------------*/
/**
 * Converts an LDIF description of a directory entry (a string value) into
 * an entry of the #Slapi_Entry type.
 *
 * A directory entry can be described by a string in LDIF format. Calling
 * the slapi_str2entry() function converts a string description in this
 * format to a #Slapi_Entry structure, which you can pass to other API
 * functions.
 *
 * \param s Description of an entry that you want to convert to a #Slapi_Entry.
 * \param flags One or more flags specifying how the entry should be generated.
 *        The valid values of the \c flags argument are:
 *        \arg #SLAPI_STR2ENTRY_REMOVEDUPVALS
 *        \arg #SLAPI_STR2ENTRY_ADDRDNVALS
 *        \arg #SLAPI_STR2ENTRY_BIGENTRY
 *        \arg #SLAPI_STR2ENTRY_TOMBSTONE_CHECK
 *        \arg #SLAPI_STR2ENTRY_IGNORE_STATE
 *        \arg #SLAPI_STR2ENTRY_INCLUDE_VERSION_STR
 *        \arg #SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES
 *        \arg #SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF
 *        \arg #SLAPI_STR2ENTRY_NO_SCHEMA_LOCK
 *        \arg #SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT
 *        \arg #SLAPI_STR2ENTRY_NO_ENTRYDN
 *        \arg #SLAPI_STR2ENTRY_DN_NORMALIZED
 *
 * \return A pointer to the #Slapi_Entry structure representing the entry.
 * \return \c NULL if the string cannot be converted; for example, if no DN is
 *         specified in the string.
 * \warning This function modifies the string argument s. If you still need to
 *          use this string value, you should make a copy of this string before
 *          calling slapi_str2entry().
 *
 * \warning When you are done working with the entry, you should call the
 *          slapi_entry_free() function.
 *
 * \note To convert an entry to a string description, call the slapi_entry2str()
 *       function.
 *
 * \see slapi_entry_free()
 * \see slapi_entry2str()
 */
Slapi_Entry *slapi_str2entry(char *s, int flags);

/*
 * Same as slapi_str2entry except passing optional dn and rdn structure as argument
 */
Slapi_Entry *slapi_str2entry_ext(const char *dn, const Slapi_RDN *srdn, char *s, int flags);


/*-----------------------------
 * Flags for slapi_str2entry()
 *----------------------------*/
/**
 * Removes any duplicate values in the attributes of the entry.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_REMOVEDUPVALS 1

/**
 * Adds the relative distinguished name (RDN) components (for example,
 * \c uid=bjensen) as attributes of the entry.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_ADDRDNVALS 2

/**
 * Provide a hint that the entry is large. This enables some optimizations
 * related to large entries.
 *
 *  \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_BIGENTRY 4

/**
 * Check to see if the entry is a tombstone. If so, set the tombstone flag
 * (#SLAPI_ENTRY_FLAG_TOMBSTONE).
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_TOMBSTONE_CHECK 8

/**
 * Ignore entry state information if present.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_IGNORE_STATE 16

/**
 * Return entries that have a <tt>version: 1</tt> line as part of the LDIF
 * representation.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_INCLUDE_VERSION_STR 32

/**
 * Add any missing ancestor values based on the object class hierarchy.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES 64

/**
 * Inform slapi_str2entry() that the LDIF input is not well formed.
 *
 * Well formed LDIF has no duplicate attribute values, already has the RDN
 * as an attribute of the entry, and has all values for a given attribute
 * type listed contiguously.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF 128

/**
 * Don't acquire the schema lock.
 *
 * You should use this flag if you are sure that the lock is already held,
 * or if the server has not started it's threads yet during startup.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_NO_SCHEMA_LOCK 256

/**
 * Normalize DN using obsolete DN normalizer.
 *
 * This marco is used only for the upgrading dn format tool.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT 512

/**
 * Do not include entrydn attribute value in the in-memory entry
 *
 * If this flag is set, entrydn in the source string is ignored.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_NO_ENTRYDN 1024

/**
 * Passed DN is already normalized.
 *
 * If this flag is set, str2entry assumes passed DN is already normalized.
 *
 * \see slapi_str2entry()
 */
#define SLAPI_STR2ENTRY_DN_NORMALIZED 2048

/**
 * Generates a description of an entry as an LDIF string.
 *
 * This function behaves much like slapi_entry2str(); however, you can specify
 * output options with this function.
 *
 * This function generates an LDIF string value conforming to the following syntax:
 * \code
 *     dn: dn\n
 *     [attr: value\n]*
 * \endcode
 *
 * For example:
 * \code
 *     dn: uid=jdoe, ou=People, dc=example,dc=com
 *     cn: Jane Doe
 *     sn: Doe
 *     ...
 * \endcode
 *
 * To convert an entry described in LDIF string format to an LDAP entry using
 * the #Slapi_Entry data type, call the slapi_str2entry() function.
 *
 * \param e Entry that you want to convert into an LDIF string.
 * \param len Length of the LDIF string returned by this function.
 * \param options An option set that specifies how you want the string
 *        converted. You can \c OR together any of the following options
 *        when you call this function:
 *        \arg #SLAPI_DUMP_STATEINFO
 *        \arg #SLAPI_DUMP_UNIQUEID
 *        \arg #SLAPI_DUMP_NOOPATTRS
 *        \arg #SLAPI_DUMP_NOWRAP
 *        \arg #SLAPI_DUMP_MINIMAL_ENCODING
 * \return The LDIF string representation of the entry you specify.
 * \return \c NULL if an error occurs.
 * \warning When you no longer need to use the string, you should free it
 *          from memory by calling the slapi_ch_free_string() function.
 *
 * \see slapi_entry2str()
 * \see slapi_str2entry()
 */
char *slapi_entry2str_with_options(Slapi_Entry *e, int *len, int options);


/*---------------------------------------------
 * Options for slapi_entry2str_with_options()
 *--------------------------------------------*/
/**
 * Output entry with replication state info.
 *
 * This allows access to the internal data used by multi-supplier replication.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_STATEINFO 1 /* replication state */

/**
 * Output entry with uniqueid.
 *
 * This option is used when creating an LDIF file to be used to initialize
 * a replica. Each entry will contain the nsuniqueID operational attribute.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_UNIQUEID 2 /* unique ID */

/**
 * Output entry without operational attributes.
 *
 * By default, certain operational attributes (such as \c creatorsName,
 * \c modifiersName, \c createTimestamp, \c modifyTimestamp) may be
 * included in the output. With this option, no operational attributes
 * will be included.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_NOOPATTRS 4 /* suppress operational attrs */

/**
 * Output entry without LDIF line wrapping.
 *
 * By default, lines will be wrapped as defined in the LDIF specification.
 * With this option, line wrapping is disabled.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_NOWRAP 8 /* no line breaks */

/**
 * Output entry with less base64 encoding.
 *
 * Uses as little base64 encoding as possible in the output.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_MINIMAL_ENCODING 16 /* use less base64 encoding */

/**
 * Output rdn based entry instead of dn based.  Introduced for subtree rename.
 *
 * \see slapi_entry2str_with_options()
 */
#define SLAPI_DUMP_RDN_ENTRY 32 /* rdn based entry */

/**
 * Generates an LDIF string description of an LDAP entry.
 *
 * This function generates an LDIF string value conforming to the following syntax:
 * \code
 *     dn: dn\n
 *     [attr: value\n]*
 * \endcode
 *
 * For example:
 * \code
 *     dn: uid=jdoe, ou=People, dc=example,dc=com
 *     cn: Jane Doe
 *     sn: Doe
 *     ...
 * \endcode
 *
 * To convert an entry described in LDIF string format to an LDAP entry using
 * the #Slapi_Entry data type, call the slapi_str2entry() function.
 *
 * \param e Entry that you want to convert into an LDIF string.
 * \param len Length of the LDIF string returned by this function.
 * \return The LDIF string representation of the entry you specify.
 * \return \c NULL if an error occurs.
 * \warning When you no longer need to use the string, you should free it
 *          from memory by calling the slapi_ch_free_string() function.
 *
 * \see slapi_entry2str_with_options()
 * \see slapi_str2entry()
 */
char *slapi_entry2str(Slapi_Entry *e, int *len);

/**
 * Allocates memory for a new entry of the data type #Slapi_Entry.
 *
 * This function returns an empty #Slapi_Entry structure. You can call other
 * front-end functions to set the DN and attributes of this entry.
 *
 * When you are no longer using the entry, you should free it from memory by
 * calling the slapi_entry_free() function.
 *
 * \return This function returns a pointer to the newly allocated entry of the
 *         data type #Slapi_Entry. If space cannot be allocated, e.g., no more
 *         virtual memory exists, the \c ns-slapd program terminates.
 * \warning When you no longer use the entry, free it from memory by calling the
 *          slapi_entry_free() function.
 *
 * \see slapi_entry_dup()
 * \see slapi_entry_free()
 */
Slapi_Entry *slapi_entry_alloc(void);

/**
 * Initializes the values of an entry with the DN and attribute value pairs you
 * supply.
 *
 * This function initializes the attributes and the corresponding attribute values
 * of an entry. Also, during the course of processing, the unique ID of the entry
 * is set to \c NULL, and the flag value is set to \c 0.
 *
 * Use this function to initialize a #Slapi_Entry pointer.
 *
 * \param e The entry you want to initialize.
 * \param dn The DN of the entry you are initializing.
 * \param a Initialization list of attribute value pairs, supplied as a
 *          #Slapi_Attr data value.
 * \warning This function should always be used after slapi_entry_alloc() and
 *          never otherwise. For example:
 * \code
 *     Slapi_Entry *e = slapi_entry_alloc();
 *     slapi_entry_init(e, NULL, NULL);
 * \endcode
 *
 * \warning To set the DN in the entry:
 * \code
 *     slapi_sdn_set_dn_passin(slapi_entry_get_sdn(e), dn);
 * \endcode
 *
 * \warning In this case, the dn argument is not copied but is consumed by the
 *          function. To copy the argument, see the following example:
 * \code
 *     char *dn = slapi_ch_strdup(some_dn);
 *     Slapi_Entry *e = slapi_entry_alloc();
 *     slapi_entry_init(e, dn, NULL);
 * \endcode
 *
 * \warning The \c dn argument is not freed in this context but will eventually
 * be freed when slapi_entry_free() is called.
 *
 * \see slapi_entry_free()
 * \see slapi_entry_alloc()
 */
void slapi_entry_init(Slapi_Entry *e, char *dn, Slapi_Attr *a);

/*
 * Initializes the values of an entry with the Slapi_DN and attribute value
 * pairs you supply.
 */
void slapi_entry_init_ext(Slapi_Entry *e, Slapi_DN *sdn, Slapi_Attr *a);

/**
 * Frees an entry, its DN, and its attributes from memory.
 *
 * Call this function to free an entry that you have allocated by using the
 * slapi_entry_alloc() function or the slapi_entry_dup() function.
 *
 * \param e Entry that you want to free.  If \c NULL, no action occurs.
 * \warning To free entries, always use this function instead of using
 *          slapi_ch_free() or free().
 *
 * \see slapi_entry_alloc()
 * \see slapi_entry_dup()
 */
void slapi_entry_free(Slapi_Entry *e);

/**
 * Makes a copy of an entry, its DN, and its attributes.
 *
 * This function returns a copy of an existing #Slapi_Entry structure. You can
 * call other front-end functions to change the DN and attributes of this entry.
 *
 * \param e Entry that you want to copy.
 * \return This function returns the new copy of the entry. If the structure
 *         cannot be duplicated, for example, if no more virtual memory exists,
 *         the \c ns-slapd program terminates.
 * \warning When you are no longer using the entry, free it from memory by
 *          calling the slapi_entry_free() function.
 * \see slapi_entry_alloc()
 * \see slapi_entry_free()
 */
Slapi_Entry *slapi_entry_dup(const Slapi_Entry *e);

/**
 * Gets the distinguished name (DN) of the specified entry.
 *
 * \param e Entry from which you want to get the DN.
 * \return This function returns the DN of the entry. This returns a pointer
 *         to the actual DN in the entry, not a copy of the DN. You should not
 *         free the DN unless you plan to replace it by calling slapi_entry_set_dn().
 * \warning Use slapi_ch_free_string() if you are replacing the DN with
 *          slapi_entry_set_dn().
 * \see slapi_ch_free_string()
 * \see slapi_entry_set_dn()
 */
char *slapi_entry_get_dn(Slapi_Entry *e);

/**
 * Returns the normalized DN from the entry that you specify.
 *
 * \param e Entry from which you want to obtain the normalized DN.
 * \return This function returns the normalized DN from the entry that you
 *         specify. If the entry you specify does not contain a normalized DN,
 *         one is created through the processing of this function.
 * \warning Never free the returned value.
 * \see slapi_entry_get_dn()
 */
char *slapi_entry_get_ndn(Slapi_Entry *e);

/**
 * Returns as a \c const the value of the #Slapi_DN object from the entry
 * that you specify.
 *
 * \param e Entry from which you want to get the #Slapi_DN object.
 * \return Returns as a \c const the #Slapi_DN object from the entry that you
 *         specify.
 * \warning Never free the returned value.  If you need a copy, use
 *          slapi_sdn_dup().
 * \see slapi_sdn_dup()
 * \see slapi_entry_get_sdn()
 */
const Slapi_DN *slapi_entry_get_sdn_const(const Slapi_Entry *e);

/**
 * Returns the #Slapi_DN object from the entry that you specify.
 *
 * \param e Entry from which you want to get the #Slapi_DN object.
 * \return Returns the #Slapi_DN object from the entry that you specify.
 * \warning Never free the returned value.  If you need a copy, use
 *          slapi_sdn_dup().
 * \see slapi_entry_get_sdn_const()
 * \see slapi_sdn_dup()
 */
Slapi_DN *slapi_entry_get_sdn(Slapi_Entry *e);

/**
 * Returns as a \c const the value of the #Slapi_RDN from the entry
 * that you specify.
 *
 * \param e Entry from which you want to get the #Slapi_RDN object.
 * \return Returns as a \c const the #Slapi_RDN object from the entry that you
 *         specify.
 * \warning Never free the returned value.  If you need a copy, use
 *          slapi_sdn_dup().
 * \see slapi_sdn_dup()
 * \see slapi_entry_get_sdn()
 */
const Slapi_RDN *slapi_entry_get_srdn_const(const Slapi_Entry *e);

/**
 * Returns the #Slapi_RDN object from the entry that you specify.
 *
 * \param e Entry from which you want to get the #Slapi_RDN object.
 * \return Returns the #Slapi_RDN object from the entry that you specify.
 * \warning Never free the returned value.  If you need a copy, use
 *          slapi_sdn_dup().
 * \see slapi_entry_get_srdn_const()
 * \see slapi_sdn_dup()
 */
Slapi_RDN *slapi_entry_get_srdn(Slapi_Entry *e);

/**
 * Returns as a \c const the DN value of the entry that you specify.
 *
 * \param e Entry from which you want to get the DN as a constant.
 * \return This function returns one of the following values:
 *         \arg The DN of the entry that you specify. The DN is returned
 *              as a const; you are not able to modify the DN value.
 *         \arg The NDN value of Slapi_DN if the DN of the Slapi_DN object is NULL.
 * \warning Never free the returned value.
 * \see slapi_entry_set_sdn()
 */
const char *slapi_entry_get_dn_const(const Slapi_Entry *e);

/**
 * Returns as a \c const the RDN value of the entry that you specify.
 *
 * \param e Entry from which you want to get the RDN as a constant.
 * \return This function returns one of the following values:
 *         \arg The RDN of the entry that you specify. The RDN is returned
 *              as a const; you are not able to modify the RDN value.
 * \warning Never free the returned value.
 * \see slapi_entry_set_srdn()
 */
const char *slapi_entry_get_rdn_const(const Slapi_Entry *e);

/**
 * Returns as a \c const the Normalized RDN value of the entry that you specify.
 *
 * \param e Entry from which you want to get the Normalized RDN as a constant.
 * \return This function returns one of the following values:
 *         \arg The Normalized RDN of the entry that you specify.
 *              The Normalized RDN is returned as a const;
 *              you are not able to modify the Normalized RDN value.
 * \warning Never free the returned value.
 * \see slapi_entry_set_srdn()
 */
const char *slapi_entry_get_nrdn_const(const Slapi_Entry *e);

/**
 * Sets the distinguished name (DN) of an entry.
 *
 * This function sets the DN pointer in the specified entry to the DN that you supply.
 *
 * \param e Entry to which you want to assign the DN.
 * \param dn Distinguished name you want assigned to the entry.
 * \warning The dn will be freed eventually when slapi_entry_free() is called.
 * \warning A copy of dn should be passed. For example:
 * \code
 *     char *dn = slapi_ch_strdup(some_dn):
 *     slapi_entry_set_dn(e, dn);
 * \endcode
 *
 * \warning The old dn will be freed as a result of this call. Do not pass in
 *          a \c NULL value.
 * \see slapi_entry_free()
 * \see slapi_entry_get_dn()
 */
void slapi_entry_set_dn(Slapi_Entry *e, char *dn);

/**
 * Sets the normalized distinguished name (DN) of an entry.
 *
 * This function sets the DN pointer in the specified entry to the DN that you supply. The DN should have been normalized.
 *
 * \param e Entry to which you want to assign the DN.
 * \warning The dn will be freed eventually when slapi_entry_free() is called.
 * \warning A copy of dn should be passed. For example:
 *
 * \warning The old dn will be freed as a result of this call. Do not pass in
 *          a \c NULL value.
 * \see slapi_entry_free()
 * \see slapi_entry_get_dn()
 */
void slapi_entry_set_normdn(Slapi_Entry *e, char *normdn);

/**
 * Sets the relative distinguished name (RDN) of an entry.
 *
 * This function sets the RDN pointer in the specified entry to the RDN that
 * you supply.
 *
 * \param e Entry to which you want to assign the RDN.
 * \param rdn Relatie distinguished name you want assigned to the entry.
 *            If dn is given here, the first rdn part is set to the RDN.
 * \warning The rdn will be copied in slapi_entry_set_rdn.
 */
void slapi_entry_set_rdn(Slapi_Entry *e, char *rdn);

/**
 * Sets the Slapi_DN value in an entry.
 *
 * This function sets the value for the #Slapi_DN object in the entry you specify.
 *
 * \param e Entry to which you want to set the value of the #Slapi_DN.
 * \param sdn The specified #Slapi_DN value that you want to set.
 * \warning This function makes a copy of the \c sdn parameter.
 * \see slapi_entry_get_sdn()
 */
void slapi_entry_set_sdn(Slapi_Entry *e, const Slapi_DN *sdn);

/**
 * Sets the Slapi_DN value containing RDN in an entry.
 *
 * This function sets the value for the #Slapi_DN object containing RDN in the entry you specify.
 *
 * \param e Entry to which you want to set the value of the #Slapi_DN.
 * \param srdn The specified #Slapi_DN value that you want to set.
 * \warning This function makes a copy of the \c srdn parameter.
 * \see slapi_entry_get_srdn()
 */
void slapi_entry_set_srdn(Slapi_Entry *e, const Slapi_RDN *srdn);


/**
 * Determines if an entry contains the specified attribute.
 *
 * If the entry contains the attribute, the function returns a pointer to
 * the attribute.
 *
 * \param e Entry that you want to check.
 * \param type Name of the attribute that you want to check.
 * \param attr Pointer to the attribute, if the attribute is found in the
 *        entry.
 * \return \c 0 if the entry contains the specified attribute.
 * \return \c -1 if the entry does not contain the specified attribute.
 * \warning Do not free the returned \c attr. It is a pointer to the internal
 *          entry data structure. It is usually wise to make a copy of the
 *          returned attribute, using slapi_attr_dup(), to avoid dangling pointers
 *          if the entry is freed while the pointer to attr is still being used.
 * \see slapi_attr_dup()
 */
int slapi_entry_attr_find(const Slapi_Entry *e, const char *type, Slapi_Attr **attr);

/**
 * Finds the first attribute in an entry.
 *
 * If you want to iterate through the attributes in an entry, use this function
 * in conjunction with the slapi_entry_next_attr() function.
 *
 * \param e Entry from which you want to get the attribute.
 * \param attr Pointer to the first attribute in the entry.
 * \return Returns 0 when successful; any other value returned signals failure.
 * \warning Do not free the returned \c attr. This is a pointer into the
 *          internal entry data structure. If you need a copy, use slapi_attr_dup().
 * \see slapi_entry_next_attr()
 * \see slapi_attr_dup()
 */
int slapi_entry_first_attr(const Slapi_Entry *e, Slapi_Attr **attr);

/**
 * Finds the next attribute after \c prevattr in an entry.
 *
 * To iterate through the attributes in an entry, use this function in conjunction
 * with the slapi_entry_first_attr() function.
 *
 * \param e Entry from which you want to get the attribute.
 * \param prevattr Previous attribute in the entry.
 * \param attr Pointer to the next attribute after \c prevattr in the entry.
 * \return \c 0 if successful.
 * \return \c -1 if \c prevattr was the last attribute in the entry.
 * \warning Do not free the returned \c attr. This is a pointer into the
 *          internal entry data structure. If you need a copy, use slapi_attr_dup().
 * \see slapi_entry_first_attr()
 * \see slapi_entry_dup()
 */
int slapi_entry_next_attr(const Slapi_Entry *e, Slapi_Attr *prevattr, Slapi_Attr **attr);

/**
 * Gets the unique ID value of the entry.
 *
 * \param e Entry from which you want to obtain the unique ID.
 * \return This function returns the unique ID value of the entry specified.
 * \warning Never free this value. If you need a copy, use slapi_ch_strdup().
 * \see slapi_entry_set_uniqueid()
 * \see slapi_ch_strdup()
 */
const char *slapi_entry_get_uniqueid(const Slapi_Entry *e);

/**
 * Replaces the unique ID value of an entry with the unique ID value that you
 * supply.
 *
 * This function replaces the unique ID value of the entry with the \c uniqueid
 * value that you specify. In addition, the function adds #SLAPI_ATTR_UNIQUEID to
 * the attribute list and gives it the unique ID value supplied. If the entry
 * already contains a #SLAPI_ATTR_UNIQUEID attribute, its value is updated with
 * the new value supplied.
 *
 * \param e Entry for which you want to generate a unique ID.
 * \param uniqueid The unique ID value that you want to assign to the entry.
 * \warning Do not free the \c uniqueid after calling this function. The value
 *          will eventually be freed when slapi_entry_free() is called.
 *
 * \warning You should pass in a copy of the value because this function will
 *          consume the value passed in. For example:
 * \code
 *     char *uniqueid = slapi_ch_strdup(some_uniqueid);
 *     slapi_entry_set_uniqueid(e, uniqueid);
 * \endcode
 *
 * \warning Do not pass in a \c NULL for \c uniqueid.
 * \see slapi_entry_get_uniqueid()
 * \see slapi_entry_free()
 */
void slapi_entry_set_uniqueid(Slapi_Entry *e, char *uniqueid);

/**
 * Determines whether the specified entry complies with the schema for its object
 * class.
 *
 * \param pb Parmeter block.
 * \param e Entry that you want to check.
 * \return \c 0 if the entry complies with the schema or if schema checking is
 *         turned off. The function also returns \c 0 if the entry has additional
 *         attributes not allowed by the schema and has the object class
 *         \c extensibleObject.
 * \return \c 1 if the entry is missing the \c objectclass attribute, if it is missing
 *         any required attributes, if it has any attributes not allowed by the schema
 *         but does not have the object class \c extensibleObject, or if the entry has
 *         multiple values for a single-valued attribute.
 * \warning The \c pb argument can be \c NULL. It is only used to get the
 *          #SLAPI_IS_REPLICATED_OPERATION flag. If that flag is present, no schema
 *          checking is done.
 */
int slapi_entry_schema_check(Slapi_PBlock *pb, Slapi_Entry *e);

/**
 * Determines whether the specified entry complies with the schema for its object
 * class.
 *
 * Like slapi_entry_schema_check() with one additional parameter to enforce schema
 * checking for replicated operations.
 * \param check_repl Set to 1 if replicted operations should be checked
 */
int slapi_entry_schema_check_ext(Slapi_PBlock *pb, Slapi_Entry *e, int check_repl);

/**
 * Determines whether the specified entry complies with the syntax rules imposed
 * by it's attribute types.
 *
 * \param pb Parameter block.
 * \param e Entry that you want to check.
 * \param override Flag to override the server configuration and force syntax checking
 *        to be performed.
 * \return \c 0 if the entry complies with the syntax rules or if syntax checking
 *         is disabled.
 * \return \c 1 if the entry has any attribute values that violate the syntax rules
 *         imposed by the associated attribute type.  If the \c pb parameter was
 *         passed in, an error message describing the syntax violations will be
 *         set in the #SLAPI_PB_RESULT_TEXT parameter.
 * \warning The \c pb parameter can be \c NULL.  It is used to store an error
 *         message with details of any syntax violations.  The \c pb parameter
 *         is also used to check if the #SLAPI_IS_REPLICATED_OPERATION flag is
 *         set.   If that flag is present, no syntax checking is performed.
 */
int slapi_entry_syntax_check(Slapi_PBlock *pb, Slapi_Entry *e, int override);

/**
 * Filter policy definitions. These define how we should check and treat filters
 * that have non-conforming attributes in the request
 * - OFF - do no check, trust the filter.
 * - WARNING - Check, and flag filter elements that are not found in schema.
 * - STRICT - check and reject filter's that have elements not found in schema.
 */
typedef enum {
    FILTER_POLICY_OFF,
    FILTER_POLICY_WARNING,
    FILTER_POLICY_PROTECT,
    FILTER_POLICY_STRICT,
} Slapi_Filter_Policy;

typedef enum {
    FILTER_SCHEMA_SUCCESS = 0,
    FILTER_SCHEMA_WARNING = 1,
    FILTER_SCHEMA_FAILURE = 2,
} Slapi_Filter_Result;

/**
 * Determine if a fiter conforms to schema, specifically, that all requested attributes
 * are in the schema.
 *
 * We assume that the filter HAS been normalised already so that the attribute names
 * match the values found in the attrsyntax hashmaps. We also base our return on the
 * provided policy.
 *
 * OFF - return SUCCESS, the filter is valid
 * WARNING - return SUCCESS, and flag filter elements that are not in schema.
 * STRICT - return SUCCESS only if all elements are found - else return FAILURE.
 */
Slapi_Filter_Result slapi_filter_schema_check(Slapi_PBlock *pb, Slapi_Filter *f, Slapi_Filter_Policy fp);

/**
 * Determines if the DN violates the Distinguished Name syntax rules.
 *
 * \param pb Parameter block.
 * \param dn The dn string you want to check.
 * \param override Flag to override the server configuration and force syntax checking
 *        to be performed.
 * \return \c 0 if the DN complies with the Distinguished Name syntax rules or if
 *         syntax checking is disabled.
 * \return \c 1 if the DN violates the Distinguished Name syntax rules.  If the \c pb
 *         parameter was passed in, an error message will be set in the
 *         #SLAPI_PB_RESULT_TEXT parameter.
 * \warning The \c pb parameter can be \c NULL.  It is used to store an error
 *         message with details of any syntax violations.  The \c pb parameter
 *         is also used to check if the #SLAPI_IS_REPLICATED_OPERATION flag is
 *         set.   If that flag is present, no syntax checking is performed.
 */
int slapi_dn_syntax_check(Slapi_PBlock *pb, const char *dn, int override);

/**
 * Determines if any values being added to an entry violate the syntax rules
 * imposed by the associated attribute type.
 *
 * \param pb Parameter block.
 * \param mods Array of mods that you want to check.
 * \param override Flag to override the server configuration and force syntax checking
 *        to be performed.
 * \return \c 0 if the mods comply with the syntax rules or if syntax checking
 *         is disabled.
 * \return \c 1 if the mods are adding any new attribute values that violate the
 *         syntax rules imposed by the associated attribute type.  If the \c pb
 *         parameter was passed in, an error message describing the syntax violations
 *         will be set in the #SLAPI_PB_RESULT_TEXT parameter.
 * \warning The \c pb parameter can be \c NULL.  It is used to store an error
 *         message with details of any syntax violations.  The \c pb parameter
 *         is also used to check if the #SLAPI_IS_REPLICATED_OPERATION flag is
 *         set.   If that flag is present, no syntax checking is performed.
 */
int slapi_mods_syntax_check(Slapi_PBlock *pb, LDAPMod **mods, int override);

/**
 * Determines whether the values in an entry’s relative distinguished name (RDN)
 * are also present as attribute values.
 *
 * For example, if the entry’s RDN is <tt>cn=Barbara Jensen</tt>, the function determines
 * if the entry has the \c cn attribute with the value <tt>Barbara Jensen</tt>.
 *
 * \param e Entry that you want to check for RDN values.
 * \return \c 1 if the values in the RDN are present in the attributes of the entry.
 * \return \c 0 if the values are not present.
 */
int slapi_entry_rdn_values_present(const Slapi_Entry *e);

/**
 * Adds the components in an entry’s relative distinguished name (RDN) to the entry
 * as attribute values.
 *
 * For example, if the entry’s RDN is <tt>uid=bjensen</tt>, the function adds
 * <tt>uid=bjensen</tt> to the entry as an attribute value.
 *
 * \param e Entry to which you want to add the RDN attributes.
 * \return \c LDAP_SUCCESS if the values were successfully added to the entry. The
 *         function also returns \c LDAP_SUCCESS if the entry is \c NULL, if the
 *         entry’s DN is \c NULL, or if the entry’s RDN is \c NULL.
 * \return \c LDAP_INVALID_DN_SYNTAX if the DN of the entry cannot be parsed.
 * \warning Free the entry from memory by using the slapi_entry_free() function, if the
 *          entry was allocated by the user.
 * \see slapi_entry_free()
 */
int slapi_entry_add_rdn_values(Slapi_Entry *e);

/**
 * Deletes an attribute (and all its associated values) from an entry.
 *
 * \param e Entry from which you want to delete the attribute.
 * \param type Attribute type that you want to delete.
 * \return \c 0 if successful.
 * \return \c 1 if the specified attribute is not part of the entry.
 * \return \c -1 if an error occurred.
 */
int slapi_entry_attr_delete(Slapi_Entry *e, const char *type);

/**
 * Gets the values of a multi-valued attribute of an entry.
 *
 * This function is very similar to slapi_entry_attr_get_charptr(), except that it
 * returns a <tt>char **</tt> array for multi-valued attributes. The array and all
 * values are copies. Even if the attribute values are not strings, they will still
 * be \c NULL terminated so that they can be used safely in a string context. If there
 * are no values, \c NULL will be returned. Because the array is \c NULL terminated,
 * the usage should be similar to the sample shown below:
 *
 * \code
 *     char **ary = slapi_entry_attr_get_charray(e, someattr);
 *     int ii;
 *     for (ii = 0; ary && ary[ii]; ++ii) {
 *        char *strval = ary[ii];
 *        ...
 *     }
 *     slapi_ch_array_free(ary);
 * \endcode
 *
 * \param e Entry from which you want to get the values.
 * \param type Attribute type from which you want to get the values.
 * \return A copy of all the values of the attribute.
 * \return \c NULL if the entry does not contain the attribute or if the attribute
 *         has no values.
 * \warning When you are done working with the values, free them from memory by calling
 *          the slapi_ch_array_free() function.
 * \see slapi_entry_attr_get_charptr()
 */
char **slapi_entry_attr_get_charray(const Slapi_Entry *e, const char *type);

/**
 * Gets the values of a multi-valued attribute of an entry.
 *
 * This function is very similar to slapi_entry_attr_get_charptr(), except that it
 * returns a <tt>char **</tt> array for multi-valued attributes. The array and all
 * values are copies. Even if the attribute values are not strings, they will still
 * be \c NULL terminated so that they can be used safely in a string context. If there
 * are no values, \c NULL will be returned. Because the array is \c NULL terminated,
 * the usage should be similar to the sample shown below:
 *
 * \code
 *     char **ary = slapi_entry_attr_get_charray(e, someattr);
 *     int ii;
 *     for (ii = 0; ary && ary[ii]; ++ii) {
 *        char *strval = ary[ii];
 *        ...
 *     }
 *     slapi_ch_array_free(ary);
 * \endcode
 *
 * \param e Entry from which you want to get the values.
 * \param type Attribute type from which you want to get the values.
 * \param numVals The number of attribute values will be stored in this variable.
 * \return A copy of all the values of the attribute.
 * \return \c NULL if the entry does not contain the attribute or if the attribute
 *         has no values.
 * \warning When you are done working with the values, free them from memory by calling
 *          the slapi_ch_array_free() function.
 * \see slapi_entry_attr_get_charptr()
 */
char **slapi_entry_attr_get_charray_ext(const Slapi_Entry *e, const char *type, int *numVals);
/**
 * Gets the first value of an attribute of an entry as a string.
 *
 * \param e Entry from which you want to get the string value.
 * \param type Attribute type from which you want to get the value.
 * \return A copy of the first value in the attribute.
 * \return \c NULL if the entry does not contain the attribute.
 * \warning When you are done working with this value, free it from memory by calling the
 *          slapi_ch_free_string() function.
 * \see slapi_entry_attr_get_charray()
 */
char *slapi_entry_attr_get_charptr(const Slapi_Entry *e, const char *type);

/**
 * Gets the first value of an attribute of an entry as a string.
 *
 * \param e Entry from which you want to get the string value.
 * \param attrname - Attribute type from which you want to get the value.
 * \return A reference of the first value in the attribute.
 * \return \c NULL if the entry does not contain the attribute.
 */
const char *slapi_entry_attr_get_ref(Slapi_Entry *e, const char *attrname);

/**
 * Gets the first value of an attribute in an entry as an integer.
 *
 * \param e Entry from which you want to get the integer value.
 * \param type Attribute type from which you want to get the value.
 * \return The first value of the attribute converted to an integer.
 * \return \c 0 if the entry does not contain the attribute.
 */
int slapi_entry_attr_get_int(const Slapi_Entry *e, const char *type);

/**
 * Gets the first value of an attribute in an entry as an unsigned integer data type.
 *
 * \param e Entry from which you want to get the integer value.
 * \param type Attribute type from which you want to get the value.
 * \return The first value of the attribute converted to an unsigned integer.
 * \return \c 0 if the entry does not contain the attribute.
 */
unsigned int slapi_entry_attr_get_uint(const Slapi_Entry *e, const char *type);

/**
 * Gets the first value of an attribute in an entry as a long data type.
 *
 * \param e Entry from which you want to get the long value.
 * \param type Attribute type from which you want to get the value.
 * \return The first value of the attribute converted to a \c long type.
 * \return \c 0 if the entry does not contain the attribute.
 */
long slapi_entry_attr_get_long(const Slapi_Entry *e, const char *type);

/**
 * Gets the first value of an attribute in an entry as an unsigned long
 * data type.
 *
 * \param e Entry from which you want to get the unsigned long value.
 * \param type Attribute type from which you want to get the value.
 * \return The first value of the attribute converted to an <tt>
 *         unsigned long</tt>.
 * \return \c 0 if the entry does not contain the attribute.
 */
unsigned long slapi_entry_attr_get_ulong(const Slapi_Entry *e, const char *type);

/**
 *  Gets the first value of an attribute in an entry as a long long data type.
 *
 *  \param e Entry from which you want to get the long long value.
 *  \param type Attribute type from which you want to get the value.
 *  \return The first value of the attribute converted to a <tt>long long</tt>.
 *  \return  \c 0 if the entry does not contain the attribute.
 */
long long slapi_entry_attr_get_longlong(const Slapi_Entry *e, const char *type);

/**
 * Gets the first value of an attribute in an entry as an unsigned
 * long long data type.
 *
 * \param e Entry from which you want to get the unsigned long long value.
 * \param type Attribute type from which you want to get the value.
 * \return The first value of the attribute converted to an <tt>unsigned
 *         long long</tt>.
 * \return  \c 0 if the entry does not contain the attribute.
 */
unsigned long long slapi_entry_attr_get_ulonglong(const Slapi_Entry *e, const char *type);

/**
 * Gets the value of a given attribute of a given entry as a boolean value.
 *
 * Comparisons are case-insensitive (\c TRUE, \c trUe, and \c true are all the
 * same), and unique substrings can be matched (\c t and \c tr will be interpreted
 * as \c true).  In addition, \c on, \c off, \c yes, \c no are supported.
 * If the attribute value is a number, then non-zero numbers are
 * interpreted as \c true, and \c 0 is interpreted as \c false.
 *
 * \param e Entry from which you want to get the boolean value.
 * \param type Attribute type from which you want to get the value.
 * \return \c PR_TRUE | \c PR_FALSE
 */
PRBool slapi_entry_attr_get_bool(const Slapi_Entry *e, const char *type);

/**
 * Gets the value of a given attribute of a given entry as a boolean value.
 *
 * Comparisons are case-insensitive (\c TRUE, \c trUe, and \c true are all the
 * same), and unique substrings can be matched (\c t and \c tr will be interpreted
 * as \c true).  In addition, \c on, \c off, \c yes, \c no are supported.
 * If the attribute value is a number, then non-zero numbers are
 * interpreted as \c true, and \c 0 is interpreted as \c false.
 * If the attribute type is not found in the entry, the given default value is returned.
 *
 * \param e Entry from which you want to get the boolean value.
 * \param type Attribute type from which you want to get the value.
 * \return \c PR_TRUE | \c PR_FALSE
 */
PRBool slapi_entry_attr_get_bool_ext(const Slapi_Entry *e, const char *type, PRBool default_value);

/**
 * Replaces the value or values of an attribute in an entry with a specified string
 * value.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param value String value that you want to assign to the attribute.
 * \warning This function makes a copy of the parameter \c value. The \c value
 *          parameter can be \c NULL; if so, this function is roughly equivalent
 *          to slapi_entry_attr_delete().
 * \see slapi_entry_attr_delete()
 */
void slapi_entry_attr_set_charptr(Slapi_Entry *e, const char *type, const char *value);

/**
 * Replaces the value or values of an attribute in an entry with a specified integer
 * data value.
 *
 * This function will replace the value or values of an attribute with the
 * integer value that you specify. If the attribute does not exist, it is created
 * with the integer value that you specify.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param l Integer value that you want to assign to the attribute.
 */
void slapi_entry_attr_set_int(Slapi_Entry *e, const char *type, int l);

/**
 * Replaces the value or values of an attribute in an entry with a specified
 * unsigned integer data type value.
 *
 * This function will replace the value or values of an attribute with the
 * unsigned integer value that you specify. If the attribute does not exist,
 * it is created with the unsigned integer value you specify.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param l Unsigned integer value that you want to assign to the attribute.
 */
void slapi_entry_attr_set_uint(Slapi_Entry *e, const char *type, unsigned int l);

/**
 * Replaces the value or values of an attribute in an entry with a specified long data
 * type value.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param l Long integer value that you want to assign to the attribute.
 */
void slapi_entry_attr_set_long(Slapi_Entry *e, const char *type, long l);

/**
 * Replaces the value or values of an attribute in an entry with a specified long long
 * data type value.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param l Long Long integer value that you want to assign to the attribute.
 */
void slapi_entry_attr_set_longlong(Slapi_Entry *e, const char *type, long long l);

/**
 * Replaces the value or values of an attribute in an entry with a specified unsigned
 * long data type value.
 *
 * This function will replace the value or values of an attribute with the unsigned
 * long value that you specify. If the attribute does not exist, it is created with the
 * unsigned long value that you specify.
 *
 * \param e Entry in which you want to set the value.
 * \param type Attribute type in which you want to set the value.
 * \param l Unsigned long value that you want to assign to the attribute.
 */
void slapi_entry_attr_set_ulong(Slapi_Entry *e, const char *type, uint64_t l);

/**
 * Check if an attribute is set in the entry
 *
 * \param e Entry that you want to check.
 * \param type Attribute type that you want to test for the value specified.
 * \return 1 if attribute is present in the entry
 * \return 0 if the attribute is not present in the entry.
 */
int slapi_entry_attr_exists(Slapi_Entry *e, const char *type);

/**
 * Determines if an attribute in an entry contains a specified value.
 *
 * The syntax of the attribute type is taken into account when checking
 * for the specified value.
 *
 * \param e Entry that you want to check.
 * \param type Attribute type that you want to test for the value specified.
 * \param value Value that you want to find in the attribute.
 * \return \c 1 if the attribute contains the specified value.
 * \return \c 0 if the attribute does not contain the specified value.
 * \warning \c value must not be \c NULL.
 */
int slapi_entry_attr_has_syntax_value(const Slapi_Entry *e, const char *type, const Slapi_Value *value);

/**
 * This function determines if the specified entry has child entries.
 *
 * \param e Entry that you want to test for child entries.
 * \return \c 1 if the entry you supply has child entries.
 * \return \c 0 if the entry you supply has child entries.
 */
int slapi_entry_has_children(const Slapi_Entry *e);

/**
 * This function determines if the specified entry has child entries
 * including the tombstoned descendents.
 *
 * \param e Entry that you want to test for child entries.
 * \param include_tombstone If non-zero, check tombstone_subordinates, too.
 * \return \c 1 if the entry you supply has child entries.
 * \return \c 0 if the entry you supply has child entries.
 */
int slapi_entry_has_children_ext(const Slapi_Entry *e, int include_tombstone);

int slapi_entry_has_conflict_children(const Slapi_Entry *e, void *plg_id);

/**
 * This function determines if an entry is the root DSE.
 *
 * The root DSE is a special entry that contains information about the Directory
 * Server, including its capabilities and configuration.
 *
 * \param dn The DN that you want to test to see if it is the root DSE entry.
 * \return \c 1 if \c dn is the root DSE.
 * \return \c 0 if \c dn is not the root DSE.
 */
int slapi_is_rootdse(const char *dn);

/**
 * This function returns the approximate size of an entry, rounded to the nearest 1k.
 *
 * This can be useful for checking cache sizes, estimating storage needs, and so on.
 *
 * When determining the size of an entry, only the sizes of the attribute values are
 * counted; the size of other entry values (such as the size of attribute names,
 * variously-normalized DNs, or any metadata) are not included in the size
 * returned. It is assumed that the size of the metadata, et al., is well enough
 * accounted for by the rounding of the size to the next largest 1k . This holds true
 * especially in larger entries, where the actual size of the attribute values far
 * outweighs the size of the metadata.
 *
 * When determining the size of the entry, both deleted values and deleted
 * attributes are included in the count.
 *
 * \param e Entry from which you want the size returned.
 * \return The size of the entry, rounded to the nearest 1k. The value returned is a
 *         size_t data type with a u_long value.
 * \return A size of 1k if the entry is empty.
 * \warning The \c e parameter must not be \c NULL.
 */
size_t slapi_entry_size(Slapi_Entry *e);

/**
 * Adds an array of #Slapi_Value data values to the existing attribute values in
 * an entry.
 *
 * If the attribute does not exist, it is created with the #Slapi_Value specified.
 *
 * \param e Entry to which you want to add values.
 * \param type Attribute type to which you want to add values.
 * \param vals \c NULL terminated array of #Slapi_Value data values you want to add.
 * \return This function returns \c 0 if successful;  any other value returned
 *         signals failure.
 * \warning This function makes a copy of the parameter \c vals. The \c vals
 *          parameter can be \c NULL.
 */
int slapi_entry_attr_merge_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals);

/**
 * Adds and array of #Slapi_Value data values to the specified attribute in an entry.
 *
 * This function adds an array of #Slapi_Value data values to an attribute. If the
 * attribute does not exist, it is created and given the value contained in the
 * #Slapi_Value array.
 *
 * \param e Entry to which you want to add values.
 * \param type Attribute type to which you want to add values.
 * \param vals \c NULL terminated array of #Slapi_Value data values you want to add.
 * \return \c LDAP_SUCCESS if the #Slapi_Value array if successfully added to the
 *         attribute.
 * \return \c LDAP_TYPE_OR_VALUE_EXISTS if any values you are trying to add duplicate
 *          an existing value in the attribute.
 * \return \c LDAP_OPERATIONS_ERROR if there are pre-existing duplicate values in the
 *         attribute.
 * \warning This function makes a copy of the parameter \c vals. The \c vals
 *          parameter can be \c NULL.
 */
int slapi_entry_add_values_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals);

/**
 * Add a Slapi_ValueSet data value to the specified attribute in an entry.
 *
 * This function adds a set of values to an attribute in an entry. The values added
 * are in the form of a #Slapi_ValueSet data type. If the entry does not contain the
 * attribute specified, it is created with the specified #Slapi_ValueSet values.
 *
 * \param e Entry to which you want to add values.
 * \param type Attribute type to which you want to add values.
 * \param vs #Slapi_ValueSet data value that you want to add to the entry.
 * \return \c 0 when successful; any other value returned signals failure.
 * \warning This function makes a copy of the parameter \c vs.  The \c vs
 *          parameter can be \c NULL.
 */
int slapi_entry_add_valueset(Slapi_Entry *e, const char *type, Slapi_ValueSet *vs);

/**
 * Removes an array of Slapi_Value data values from an attribute in an entry.
 *
 * This function removes an attribute/valueset from an entry. Both the attribute
 * and its #Slapi_Value data values are removed from the entry. If you supply a
 * #Slapi_Value whose value is \c NULL, the function will delete the specified
 * attribute from the entry. In either case, the function returns \c LDAP_SUCCESS.
 *
 * \param e Entry from which you want to delete values.
 * \param type Attribute type from which you want to delete values.
 * \param vals \c NULL terminated array of #Slapi_Value data values that you
 *             want to delete.
 * \return \c LDAP_SUCCESS if the specified attribute and the array of #Slapi_Value
 *         data values are deleted from the entry.
 * \return If the specified attribute contains a \c NULL value, the attribute is
 *         deleted from the attribute list, and the function returns
 *         \c LDAP_NO_SUCH_ATTRIBUTE. As well, if the attribute is not found in the
 *         list of attributes for the specified entry, the function returns
 *         \c LDAP_NO_SUCH_ATTRIBUTE.
 * \return If there is an operational error during the processing of this call such
 *         as a duplicate value found, the function will return
 *         \c LDAP_OPERATIONS_ERROR.
 * \warning The \c vals parameter can be \c NULL, in which case this function does
 *          nothing.
 */
int slapi_entry_delete_values_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals);

/**
 * Merges (adds) and array of #Slapi_Value data values to a specified attribute in
 * an entry.
 *
 * This function adds additional #Slapi_Value data values to the existing values
 * contained in an attribute. If the attribute type does not exist, it is created.
 *
 * If the specified attribute exists in the entry, the function merges the value
 * specified and returns \c LDAP_SUCCESS. If the attribute is not found in the entry,
 * the function creates it with the #Slapi_Value specified and returns \c
 * LDAP_NO_SUCH_ATTRIBUTE.
 *
 * If this function fails, it leaves the values for \c type within a pointer to
 * \c e in an indeterminate state. The present valueset may be truncated.
 *
 * \param e Entry into which you want to merge values.
 * \param type Attribute type that you want to merge the values into.
 * \param vals \c NULL terminated array of #Slapi_Value values that you want to merge
 *             into the entry.
 * \return \c LDAP_SUCCESS
 * \return \c LDAP_NO_SUCH_ATTRIBUTE
 * \warning This function makes a copy of \c vals.  The \c vals parameter
 *          can be \c NULL.
 */
int slapi_entry_merge_values_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals);

/**
 * Replaces the values of an attribute with the #Slapi_Value data value you specify.
 *
 * This function replaces existing attribute values in a specified entry with a single
 * #Slapi_Value data value. The function first deletes the existing attribute from the
 * entry, then replaces it with the new value specified.
 *
 * \param e Entry in which you want to replace values.
 * \param type Attribute type which will receive the replaced values
 * \param vals \c NULL terminated array of #Slapi_Value valyes that should replace
 *             the existing values of the attribute.
 * \return \c 0 when successful; any other value returned signals failure.
 * \warning This function makes a copy of \c vals.  The \c vals parameter
 *          can be \c NULL.
 */
int slapi_entry_attr_replace_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals);

/**
 * Adds a specified #Slapi_Value data value to an attribute in an entry.
 *
 * This function adds a #Slapi_Value data value to the existing attribute values in an
 * entry. If the specified attribute does not exist in the entry, the attribute is
 * created with the #Slapi_Value specified. The function doesn’t check for duplicate
 * values, meaning it does not check if the value being added is already there.
 *
 * \param e Entry to which you want to add a value.
 * \param type Attribute to which you want to add a value.
 * \param value The #Slapi_Value data value you want to add to the entry.
 * \return \c 0 when successful; any other value returned signals failure.
 * \warning This function makes a copy of \c value.  The \c value parameter
 *          can be \c NULL.
 */
int slapi_entry_add_value(Slapi_Entry *e, const char *type, const Slapi_Value *value);

/**
 * Adds a string value to an attribute in an entry.
 *
 * This function adds a string value to the existing attribute values in an entry. If
 * the specified attribute does not exist in the entry, the attribute is created with
 * the string value specified. The function doesn’t check for duplicate values; it
 * does not check if the string value being added is already there.
 *
 * \param e Entry to which you want to add a string value.
 * \param type Attribute to which you want to add a string value.
 * \param value String value you want to add.
 * \return \c 0 when successful; any other value returned signals failure.
 * \warning This function makes a copy of \c value.  The \c value parameter
 *          can be \c NULL.
 */
int slapi_entry_add_string(Slapi_Entry *e, const char *type, const char *value);

/**
 * Deletes a string value from an attribute in an entry.
 *
 * \param e Entry from which you want the string deleted.
 * \param type Attribute type from which you want the string deleted.
 * \param value Value of string to delete.
 * \return \c 0 when successful; any other value returned signals failure.
 */
int slapi_entry_delete_string(Slapi_Entry *e, const char *type, const char *value);

/**
 * Find differences between two entries.
 *
 * Compares two #Slapi_Entry entries and determines the difference between them.  The
 * differences are returned as the modifications needed to the first entry to make it
 * match the second entry.
 *
 * \param smods An empty #Slapi_Mods that will be filled in with the modifications
 *              needed to make \c e1 the same as \c e2.
 * \param e1 The first entry you want to compare.
 * \param e2 The second entry you want to compare.
 * \param diff_ctrl Allows you to skip comparing operational attributes by passing
 *                  #SLAPI_DUMP_NOOPATTRS.  Pass \c 0 if you want to compare the
 *                  operational attributes.
 * \warning The caller must allocate the #Slapi_Mods that is passed in as \c smods.
 *          This must be an empty #Slapi_Mods, otherwise the contents will be leaked.
 * \warning It is up to the caller to free \c smods when they are finished using them
 *          by calling slapi_mods_free() or slapi_mods_done() if \c smods was allocated
 *          on the stack.
 */
void slapi_entry_diff(Slapi_Mods *smods, Slapi_Entry *e1, Slapi_Entry *e2, int diff_ctrl);

/**
 * Applies an array of \c LDAPMod modifications to a Slapi_Entry.
 *
 * \param e Entry to which you want to apply the modifications.
 * \param mods \c NULL terminated array of \c LDAPMod modifications that you
 *             want to apply to the specified entry.
 * \return \c LDAP_SUCCESS if the mods applied to the entry cleanly, otherwise an
 *         LDAP error is returned.
 * \warning It is up to the caller to free the \c LDAPMod array after the mods have
 *          been applied.
 */
int slapi_entry_apply_mods(Slapi_Entry *e, LDAPMod **mods);

/**
 * Applies a single \c LDAPMod modification to a Slapi_Entry.
 *
 * \param e Entry to which you want to apply the modification.
 * \param mod A pointer to the \c LDAPMod modification that you
 *             want to apply to the specified entry.
 * \return \c LDAP_SUCCESS if the mod applied to the entry cleanly, otherwise
 *         an LDAP error is returned.
 * \warning It is up to the caller to free the \c LDAPMod after the mod has
 *          been applied.
 */
int slapi_entry_apply_mod(Slapi_Entry *e, LDAPMod *mod);

/**
 * Renames a Slapi_Entry.
 *
 * This function will rename an existing \c Slapi_Entry, similar to what
 * would happen with a \c MODRDN operation.  New RDN values will be added
 * as attributes to the entry and old RDN values will be deleted if requested.
 *
 * \param e Entry that you want to rename.
 * \param newrdn The new RDN value to be used for renaming the entry.  This must
 *               not be \c NULL.
 * \param deleteoldrdn Will delete the old RDN values from the entry if set to \c 1.
 * \param newsuperior The new superior DN to use when renaming the entry.  Set this
 *                    to \c NULL if you do not want to move the entry.
 * \return \c LDAP_SUCCESS if the rename was successful, otherwise an LDAP error
 *         is returned.
 */
int slapi_entry_rename(Slapi_Entry *e, const char *newrdn, int deleteoldrdn, Slapi_DN *newsuperior);


/*------------------------
 * Entry flags.
 *-----------------------*/
/**
 * Flag that signifies that an entry is a tombstone entry
 *
 * \see slapi_entry_flag_is_set()
 * \see slapi_entry_set_flag()
 * \see slapi_entry_clear_flag()
 */
#define SLAPI_ENTRY_FLAG_TOMBSTONE    0x1
#define SLAPI_ENTRY_FLAG_LDAPSUBENTRY 0x2
#define SLAPI_ENTRY_FLAG_DIFF_IN_BOTH 0x4
#define SLAPI_ENTRY_FLAG_REFERRAL     0x8

/**
 * Determines if certain flags are set for a specified entry.
 *
 * \param e Entry for which you want to check for the specified flag.
 * \param flag The flag whose presense you want to check for. Valid flags are:
 *        \arg #SLAPI_ENTRY_FLAG_TOMBSTONE
 * \return \c 0 if the flag is not set.
 * \return The value of the flag if it is set.
 * \see slapi_entry_clear_flag()
 * \see slapi_entry_set_flag()
 */
int slapi_entry_flag_is_set(const Slapi_Entry *e, unsigned char flag);

/**
 * Sets a flag for a specified entry.
 *
 * \param e Entry for which you want to set the flag.
 * \param flag Flag that you want to set. Valid flags are:
 *        \arg #SLAPI_ENTRY_FLAG_TOMBSTONE
 * \see slapi_entry_clear_flag()
 * \see slapi_entry_flag_is_set()
 */
void slapi_entry_set_flag(Slapi_Entry *e, unsigned char flag);

/**
 * Clears a flag for a specified entry.
 *
 * \param e Entry for which you want to clear the flag.
 * \param flag Flag that you want to clear.  Valid flags are:
 *        \arg #SLAPI_ENTRY_FLAG_TOMBSTONE
 * \see slapi_entry_flag_is_set()
 * \see slapi_entry_set_flag()
 */
void slapi_entry_clear_flag(Slapi_Entry *e, unsigned char flag);


/*------------------------------
 * exported vattrcache routines
 *------------------------------*/
/**
 * Check if an entry is current in the virtual attribute cache.
 *
 * \param e The entry for which you want to check the virtual attribute cache
 *          validity.
 * \return \c 1 if the entry is valid in the cache.
 * \return \c 0 if the entry is invalid in the cache.
 */
int slapi_entry_vattrcache_watermark_isvalid(const Slapi_Entry *e);

/**
 * Mark an entry as valid in the virtual attribute cache.
 *
 * \param e The entry that you want to mark as valid.
 */
void slapi_entry_vattrcache_watermark_set(Slapi_Entry *e);

/**
 * Mark an entry as invalid in the virtual attribute cache.
 *
 * \param e The entry that you want to mark as invalid.
 */
void slapi_entry_vattrcache_watermark_invalidate(Slapi_Entry *e);

/**
 * Apply mod and store the result in the entry extension
 *
 * \param e The entry to apply mod.
 * \param mod The mod to apply on entry.
 * \param modcnt The count of values in mod.
 *
 * \return \c 1 if mod is applied and stored in extension.
 * \return \c -1 if mod is applied and failed
 * \return \c 0 if mod is nothing to do with extension
 */
int slapi_entry_apply_mod_extension(Slapi_Entry *e, const LDAPMod *mod, int modcnt);

/**
 * Invalidate all entries in the virtual attribute cache.
 */
void slapi_entrycache_vattrcache_watermark_invalidate(void);


/*
 * Slapi_DN routines
 */
/**
 * Creates a new \c Slapi_DN structure.
 *
 * This function will allocate the necessary memory for a \c Slapi_DN
 * and initialize both the DN and normalized DN values to \c NULL.
 *
 * \return A pointer to the newly allocated, and still empty,
 *         \c Slapi_DN structure.
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new(void);

/**
 * Creates a new \c Slapi_DN structure and intializes it's DN to a requested value.
 *
 * The DN of the new structure will point to a copy of the string pointed to by \c dn.
 * The DN value is passed in to the parameter by value.
 *
 * \param dn The DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         a DN value set to the content of \c dn.
 * \warning The \c dn value is copied by the function itself.  The caller
 *          is still responsible for the memory used by \c dn.
 * \see slapi_sdn_new_dn_byref()
 * \see slapi_sdn_new_dn_passin()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_dn_byval(const char *dn);

Slapi_DN *slapi_sdn_new_from_char_dn(const char *dn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized DN to a requested value.
 *
 * The normalized DN of the new structure will point to a copy of the string pointed to by
 * \c ndn.  The normalized DN value is passed in to the parameter by value.
 *
 * \param ndn The normalized DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         the normalized DN value set to the content of \c ndn.
 * \warning The \c ndn value is copied by the function itself.  The caller
 *          is still responsible for the memory used by \c ndn.
 * \see slapi_sdn_new_ndn_byref()
 * \see slapi_sdn_new_ndn_passin()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_ndn_byval(const char *ndn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's DN to a requested value.
 *
 * The DN of the new structure will point to the same string pointed to by \c dn.
 * The DN value is passed in to the parameter by reference.
 *
 * \param dn The DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         a DN value set to the content of \c dn.
 * \warning The caller is still responsible for the memory used by \c dn.  This
 *          memory should not be freed until the returned \c Slapi_DN has been
 *          disposed of or reinitialized.
 * \see slapi_sdn_new_dn_byval()
 * \see slapi_sdn_new_dn_passin()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_dn_byref(const char *dn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized and case ignored DN to a requested value.
 *
 * The normalized and case ignored DN of the new structure will point to the same string pointed to by \c ndn.
 * The normalized and case ignored DN value is passed in to the parameter by reference.
 *
 * \param ndn The normalized and case ignored DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         the normalized and case ignored DN value set to the content of \c ndn.
 * \warning The caller is still responsible for the memory used by \c ndn.  This
 *          memory should not be freed until the returned \c Slapi_DN has been
 *          disposed of or reinitialized.
 * \see slapi_sdn_new_ndn_byval()
 * \see slapi_sdn_new_ndn_passin()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_ndn_byref(const char *ndn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized and case ignored DN to a requested value.
 *
 * The normalized and case ignored DN of the new structure will point to the same string pointed to by \c ndn.
 * Ownership of the memory pointed to by \c ndn is tranferred to the Slapi_DN.
 *
 * \param ndn The normalized and case ignored DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         the normalized and case ignored DN value set to the content of \c ndn.
 * \see slapi_sdn_new_ndn_byval()
 * \see slapi_sdn_new_ndn_byref()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_ndn_passin(const char *ndn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's DN to a requested value.
 *
 * The DN of the new structure will point to the same string pointed to by \c dn.
 * Ownership of the memory pointed to by \c dn is tranferred to the Slapi_DN.
 *
 * \param dn The DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         a DN value set to the content of \c dn.
 * \warning The caller is no longer responsible for the memory used by \c dn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_new_dn_byval()
 * \see slapi_sdn_new_dn_byref()
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_dn_passin(const char *dn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized DN to a requested value.
 *
 * The DN of the new structure will point to the same string pointed to by \c normdn.
 * The normalized DN value is passed in to the parameter by reference.
 *
 * \param normdn The normalized DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         the normalized DN value set to the content of \c normdn.
 * \warning The caller is still responsible for the memory used by \c normdn.
 *          This memory should not be freed until the returned \c Slapi_DN
 *          has been disposed of or reinitialized.
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_normdn_byref(const char *normdn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized DN to a requested value.
 *
 * The DN of the new structure will point to the same string pointed to by \c normdn.
 * Ownership of the memory pointed to by \c normdn is tranferred to the Slapi_DN.
 *
 * \param normdn The DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         a DN value set to the content of \c dn.
 * \warning The caller is no longer responsible for the memory used by \c dn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_normdn_passin(const char *dn);

/**
 * Creates a new \c Slapi_DN structure and intializes it's normalized DN to a requested value.
 *
 * The DN of the new structure will point to the duplicated string of \c normdn.
 * Ownership of the memory pointed to by \c normdn is tranferred to the Slapi_DN.
 *
 * \param normdn The copy of the DN value to be set in the new \c Slapi_DN structure.
 * \return A pointer to the newly allocated \c Slapi_DN structure with
 *         a DN value set to the content of \c dn.
 * \warning The caller is no longer responsible for the memory used by \c dn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_free()
 * \see slapi_sdn_copy()
 * \see slapi_sdn_done()
 */
Slapi_DN *slapi_sdn_new_normdn_byval(const char *dn);

/**
 * Sets a DN value in a \c Slapi_DN structure.
 *
 * The DN of the structure will point to a copy of the string pointed to by
 * \c dn.  The DN value is passed in to the parameter by value.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param dn The DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The \c dn value is copied by the function itself.  The caller
 *          is still responsible for the memory used by \c dn.
 * \see slapi_sdn_set_dn_byref()
 * \see slapi_sdn_set_dn_passin()
 */
Slapi_DN *slapi_sdn_set_dn_byval(Slapi_DN *sdn, const char *dn);

/**
 * Sets a DN value in a \c Slapi_DN structure.
 *
 * The DN of the structure will point to the same string pointed to by \c dn.
 * The DN value is passed in to the parameter by reference.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param dn The DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The caller is still responsible for the memory used by \c dn.  This
 *          memory should not be freed until the returned \c Slapi_DN has been
 *          disposed of or reinitialized.
 * \see slapi_sdn_set_dn_byval()
 * \see slapi_sdn_set_dn_passin()
 */
Slapi_DN *slapi_sdn_set_dn_byref(Slapi_DN *sdn, const char *dn);

/**
 * Sets a DN value in a \c Slapi_DN structure.
 *
 * The DN of the structure will point to the same string pointed to by \c dn.
 * Ownership of the memory pointed to by \c dn is tranferred to the Slapi_DN.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param dn The DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The caller is no longer responsible for the memory used by \c dn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_set_dn_byval()
 * \see slapi_sdn_set_dn_byref()
 */
Slapi_DN *slapi_sdn_set_dn_passin(Slapi_DN *sdn, const char *dn);

/**
 * Sets a normalized DN value in a \c Slapi_DN structure.
 *
 * The DN of the structure will point to the same string pointed to by \c normdn.
 * The normalized DN value is passed in to the parameter by reference.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param normdn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The caller is still responsible for the memory used by \c normdn.
 *          This memory should not be freed until the returned \c Slapi_DN
 *          has been disposed of or reinitialized.
 * \see slapi_sdn_set_dn_byref()
 */
Slapi_DN *slapi_sdn_set_normdn_byref(Slapi_DN *sdn, const char *normdn);

/**
 * Sets a normalized DN value in a \c Slapi_DN structure.
 *
 * The DN of the structure will point to the same string pointed to by \c normdn.
 * Ownership of the memory pointed to by \c normdn is tranferred to the Slapi_DN.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param normdn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The caller is no longer responsible for the memory used by \c normdn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_set_dn_passin()
 */
Slapi_DN *slapi_sdn_set_normdn_passin(Slapi_DN *sdn, const char *dn);

/**
 * Sets a normalized DN value in a \c Slapi_DN structure.
 *
 * The normalized DN of the structure will point to a copy of the string
 * pointed to by \c dn.  The normalized DN value is passed in to the parameter
 * by value.
 * Ownership of the memory pointed to by \c normdn is tranferred to the Slapi_DN.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param normdn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new DN value.
 * \warning The caller is no longer responsible for the memory used by \c normdn.
 *          This memory should not be freed directly.  It will be freed when
 *          the \c Slapi_DN is properly disposed of.
 * \see slapi_sdn_set_dn_passin()
 */
Slapi_DN *slapi_sdn_set_normdn_byval(Slapi_DN *sdn, const char *dn);

/**
 * Sets a normalized and case ignored DN value in a \c Slapi_DN structure.
 *
 * The normalized DN of the structure will point to a copy of the string
 * pointed to by \c ndn.  The normalized DN value is passed in to the parameter
 * by value.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param ndn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new normalized DN value.
 * \warning The \c ndn value is copied by the function itself.  The caller
 *          is still responsible for the memory used by \c ndn.
 * \see slapi_sdn_set_ndn_byref()
 * \see slapi_sdn_set_ndn_passin()
 */
Slapi_DN *slapi_sdn_set_ndn_byval(Slapi_DN *sdn, const char *ndn);

/**
 * Sets a normalized DN value in a \c Slapi_DN structure.
 *
 * The normalized DN of the structure will point to the same string pointed to
 * by \c ndn.  The normalized DN value is passed in to the parameter by reference.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param ndn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new normalized DN value.
 * \warning The caller is still responsible for the memory used by \c ndn.  This
 *          memory should not be freed until the returned \c Slapi_DN has been
 *          disposed of or reinitialized.
 * \see slapi_sdn_set_ndn_byval()
 * \see slapi_sdn_set_ndn_passin()
 */
Slapi_DN *slapi_sdn_set_ndn_byref(Slapi_DN *sdn, const char *ndn);

/**
 * Sets a normalized DN value in a \c Slapi_DN structure.
 *
 * The normalized DN of the structure will point to the same string pointed to
 * by \c ndn.
 * Ownership of the memory pointed to by \c ndn is tranferred to the Slapi_DN.
 *
 * \param sdn The target \c Slapi_DN structure.
 * \param ndn The normalized DN value to be set in \c sdn.
 * \return A pointer to the \c Slapi_DN structure containing the new normalized DN value.
 * \see slapi_sdn_set_ndn_byval()
 * \see slapi_sdn_set_ndn_byref()
 */
Slapi_DN *slapi_sdn_set_ndn_passin(Slapi_DN *sdn, const char *ndn);

/**
 * Clears the contents of a Slapi_DN structure.
 *
 * Both the DN and the normalized DN are freed if the \c Slapi_DN structure
 * owns the memory.  Both pointers are then set to \c NULL.
 *
 * \param sdn Pointer to the \c Slapi_DN to clear.
 * \see slapi_sdn_free()
 */
void slapi_sdn_done(Slapi_DN *sdn);

/**
 * Frees a \c Slapi_DN structure.
 *
 * Both the DN and the normalized DN are freed if the \c Slapi_DN structure
 * owns the memory.  The \c Slapi_DN structure itself is then freed.
 *
 * \param sdn Pointer to the pointer of the \c Slapi_DN structure to be freed.
 * \see slapi_sdn_done()
 * \see slapi_sdn_new()
 */
void slapi_sdn_free(Slapi_DN **sdn);

/**
 * Retrieves the normalized DN value of a \c Slapi_DN structure.
 *
 * If the structure does not contain a normalized DN yet, it will normalize
 * the un-normalized DN and set it in the structure.
 *
 * \param sdn The \c Slapi_DN strucure containing the DN value.
 * \return A pointer to the DN value if one is set.
 * \return A pointer to the normalized and case ignored DN value if one is set and no
 *         DN value is set.
 * \return \c NULL if no normalized DN or normalized and case ignored DN value is set.
 * \warning The pointer returned is the actual value from the structure, not a copy.
 * \see slapi_sdn_get_ndn()
 * \see slapi_sdn_get_udn()
 */
const char *slapi_sdn_get_dn(const Slapi_DN *sdn);

/**
 * Retrieves the normalized and case ignored DN value of a \c Slapi_DN structure.
 *
 * If the structure does not contain a normalized and case ignored DN yet,
 * it will normalize and case ignore the DN and set it in the structure.
 *
 * \param sdn The \c Slapi_DN strucure containing the normalized and case ignored DN value.
 * \return The normalized DN value.
 * \return \c NULL if no DN or normalized DN value is set.
 * \warning The pointer returned is the actual value from the structure, not a copy.
 * \see slapi_sdn_get_dn()
 * \see slapi_sdn_get_udn()
 */
const char *slapi_sdn_get_ndn(const Slapi_DN *sdn);

/**
 * Retrieves the un-normalized DN value of a \c Slapi_DN structure.
 *
 * \param sdn The \c Slapi_DN strucure containing the DN value.
 * \return A pointer to the DN value if one is set.
 * \return A pointer to the normalized DN value if one is set and no
 *         DN value is set.
 * \return \c NULL if no DN or normalized DN value is set.
 * \warning The pointer returned is the actual value from the structure, not a copy.
 * \see slapi_sdn_get_dn()
 * \see slapi_sdn_get_ndn()
 */
const char *slapi_sdn_get_udn(const Slapi_DN *sdn);

/**
 * Fills in an existing \c Slapi_DN structure with the parent DN of the passed in \c Slapi_DN.
 *
 * \param sdn Pointer to the \c Slapi_DN structure containing the DN whose parent is desired.
 * \param sdn_parent Pointer to the \c Slapi_DN structure where the parent DN is returned.
 *        The existing contents (if any) will be cleared before the new DN value is set.
 * \warning A \c Slapi_DN structure for \c sdn_parent must be allocated before calling this function.
 * \see slapi_sdn_get_backend_parent()
 */
void slapi_sdn_get_parent(const Slapi_DN *sdn, Slapi_DN *sdn_parent);

/**
 * Fills in an existing \c Slapi_DN structure with the parent DN of the passed in \c Slapi_DN.
 *
 * \param sdn Pointer to the \c Slapi_DN structure containing the DN whose parent is desired.
 * \param sdn_parent Pointer to the \c Slapi_DN structure where the parent DN is returned.
 *        The existing contents (if any) will be cleared before the new DN value is set.
 * \param is_tombstone If non-zero, the special leaf rdn "nsuniqueid=.." is ignored.
 * \warning A \c Slapi_DN structure for \c sdn_parent must be allocated before calling this function.
 * \see slapi_sdn_get_backend_parent()
 */
void slapi_sdn_get_parent_ext(const Slapi_DN *sdn, Slapi_DN *sdn_parent, int is_tombstone);

/**
 * Fills in an existing \c Slapi_DN structure with the parent DN of an entry within a specific backend.
 *
 * The parent DN is returned in \c sdn_parent, unless \c sdn is empty or is a suffix of the backend
 * itself. In this case, \c sdn_parent is empty.
 *
 * \param sdn Pointer to the \c Slapi_DN structure containing the DN whose parent is desired.
 * \param sdn_parent Pointer to the \c Slapi_DN structure where the parent DN is returned.
 *        The existing contents (if any) will be cleared before the new DN value is set.
 * \param backend Backend to search for the parent of \c sdn.
 * \warning A \c Slapi_DN structure for \c sdn_parent must be allocated before calling this function.
 * \see slapi_sdn_get_parent()
 */
void slapi_sdn_get_backend_parent(const Slapi_DN *sdn, Slapi_DN *sdn_parent, const Slapi_Backend *backend);

/**
 * Fills in an existing \c Slapi_DN structure with the parent DN of an entry within a specific backend.
 *
 * The parent DN is returned in \c sdn_parent, unless \c sdn is empty or is a suffix of the backend
 * itself. In this case, \c sdn_parent is empty.
 *
 * \param sdn Pointer to the \c Slapi_DN structure containing the DN whose parent is desired.
 * \param sdn_parent Pointer to the \c Slapi_DN structure where the parent DN is returned.
 *        The existing contents (if any) will be cleared before the new DN value is set.
 * \param backend Backend to search for the parent of \c sdn.
 * \param is_tombstone If non-zero, the special leaf rdn "nsuniqueid=.." is ignored.
 * \warning A \c Slapi_DN structure for \c sdn_parent must be allocated before calling this function.
 * \see slapi_sdn_get_parent()
 */
void slapi_sdn_get_backend_parent_ext(const Slapi_DN *sdn, Slapi_DN *sdn_parent, const Slapi_Backend *backend, int is_tombstone);

/**
 * Return the size of a \c Slapi_DN structure.
 *
 * \param sdn Pointer to the \c Slapi_DN structure to get the size.
 */
size_t slapi_sdn_get_size(const Slapi_DN *sdn);

/**
 * Duplicates a \c Slapi_DN structure.
 *
 * \param sdn Pointer to the \c Slapi_DN structure to duplicate.
 * \return A pointer to a duplicate of \c sdn.
 * \see slapi_sdn_copy()
 * \see slapi_sdn_new()
 * \see slapi_sdn_free()
 */
Slapi_DN *slapi_sdn_dup(const Slapi_DN *sdn);

/**
 * Copies the contents of a \c Slapi_DN to another \c existing Slapi_DN.
 *
 * \param from A pointer to the \c Slapi_DN structure to copy from.
 * \param to A pointer to the \c Slapi_DN structure to copy to.
 * \warning You must allocate \c to before calling this function.
 * \see slapi_sdn_dup()
 */
void slapi_sdn_copy(const Slapi_DN *from, Slapi_DN *to);

/**
 * Compares two \c Slapi_DN structures.
 *
 * Performs a case-sensitive comparison of two \c Slapi_DN structures.
 *
 * \param sdn1 A pointer to the first \c Slapi_DN structure to compare.
 * \param sdn2 A pointer to the second \c Slapi_DN structure to compare.
 * \return \c 0 if \c sdn1 is equal to \c sdn2.
 * \return \c -1 if \c sdn1 is \c NULL.
 * \return \c 1 if \c sdn2 is \c NULL and \c sdn1 is not \c NULL.
 */
int slapi_sdn_compare(const Slapi_DN *sdn1, const Slapi_DN *sdn2);

/**
 * Checks if a DN or normalized DN is set in a \c Slapi_DN.
 *
 * \param sdn A pointer to the \c Slapi_DN structure to check.
 * \return \c 1 if there is no DN or normalized DN set in \c sdn.
 * \return \c 0 if \c sdn is not empty.
 * \see slapi_sdn_done()
 */
int slapi_sdn_isempty(const Slapi_DN *sdn);

/**
 * Checks whether a \c Slapi_DN structure contains a suffix of another \c Slapi_DN structure.
 *
 * \param sdn A pointer to the \c Slapi_DN structure to check.
 * \param suffixsdn A pointer to the \c Slapi_DN structure of the suffix.
 * \return \c 1 if the DN in \c suffixsdn is a suffix of \c sdn.
 * \return \c 0 if the DN in \c suffixsdn is not a suffix of \c sdn.
 * \see slapi_sdn_isparent()
 * \see slapi_sdn_isgrandparent()
 */
int slapi_sdn_issuffix(const Slapi_DN *sdn, const Slapi_DN *suffixsdn);

/**
 * Checks whether a DN is the parent of a given DN.
 *
 * \param parent A pointer to the \c Slapi_DN structure containing the DN
 *        which claims to be the parent of the DN in \c child.
 * \param child A pointer to the Slapi_DN structure containing the DN of the
 *        supposed child of the DN in the structure pointed to by \c parent.
 * \return \c 1 if the DN in \c parent is the parent of the DN in \c child.
 * \return \c 0 if the DN in \c parent is not the parent of the DN in \c child.
 * \see slapi_sdn_isgrandparent()
 * \see slapi_sdn_issuffix()
 * \see slapi_sdn_get_parent()
 */
int slapi_sdn_isparent(const Slapi_DN *parent, const Slapi_DN *child);

/**
 * Checks whether a DN is the grandparent of a given DN.
 *
 * \param parent A pointer to the \c Slapi_DN structure containing the DN
 *        which claims to be the grandparent of the DN in \c child.
 * \param child A pointer to the Slapi_DN structure containing the DN of the
 *        supposed grandchild of the DN in the structure pointed to by \c parent.
 * \return \c 1 if the DN in \c parent is the grandparent of the DN in \c child.
 * \return \c 0 if the DN in \c parent is not the grandparent of the DN in \c child.
 * \see slapi_sdn_isparent()
 * \see slapi_sdn_issuffix()
 * \see slapi_sdn_get_parent()
 */
int slapi_sdn_isgrandparent(const Slapi_DN *parent, const Slapi_DN *child);

/**
 * Gets the length of the normalized DN of a Slapi_DN structure.
 *
 * This function normalizes \c sdn if it has not already been normalized.
 *
 * \param sdn A pointer to the \c Slapi_DN structure containing the DN value.
 * \return The length of the normalized DN.
 */
int slapi_sdn_get_ndn_len(const Slapi_DN *sdn);

/**
 * Checks if a DN is within a specified scope under a specified base DN.
 *
 * \param dn A pointer to the \c Slapi_DN structure to test.
 * \param base The base DN against which \c dn is going to be tested.
 * \param scope The scope tested.  Valid scopes are:
 *        \arg \c LDAP_SCOPE_BASE
 *        \arg \c LDAP_SCOPE_ONELEVEL
 *        \arg \c LDAP_SCOPE_SUBTREE
 * \return non-zero if \c dn matches the scoping criteria given by \c base and \c scope.
 * \see slapi_sdn_compare()
 * \see slapi_sdn_isparent()
 * \see slapi_sdn_issuffix()
 */
int slapi_sdn_scope_test(const Slapi_DN *dn, const Slapi_DN *base, int scope);

/**
 * Checks if a DN is within a specified scope under a specified base DN.
 * This api adjusts tombstoned DN when comparing with the base dn.
 *
 * \param dn A pointer to the \c Slapi_DN structure to test.
 * \param base The base DN against which \c dn is going to be tested.
 * \param scope The scope tested.  Valid scopes are:
 *        \arg \c LDAP_SCOPE_BASE
 *        \arg \c LDAP_SCOPE_ONELEVEL
 *        \arg \c LDAP_SCOPE_SUBTREE
 * \param flags 0 or SLAPI_ENTRY_FLAG_TOMBSTONE
 * \return non-zero if \c dn matches the scoping criteria given by \c base and \c scope.
 * \see slapi_sdn_compare()
 * \see slapi_sdn_isparent()
 * \see slapi_sdn_issuffix()
 */
int slapi_sdn_scope_test_ext(const Slapi_DN *dn, const Slapi_DN *base, int scope, int flags);

/**
 * Retrieves the RDN from a given DN.
 *
 * This function takes the DN stored in the \c Slapi_DN structure pointed to
 * by \c sdn and fills in it's RDN within the \c Slapi_RDN structure pointed
 * to by \c rdn.
 *
 * \param sdn A pointer to the \c Slapi_DN structure containing the DN.
 * \param rdn A pointer to the \c Slapi_RDN structure where the RDN is filled in.
 * \warning The caller must allocate \c rdn before calling this function.
 * \see slapi_sdn_get_dn()
 * \see slapi_sdn_set_rdn()
 * \see slapi_sdn_add_rdn()
 * \see slapi_sdn_is_rdn_component()
 */
void slapi_sdn_get_rdn(const Slapi_DN *sdn, Slapi_RDN *rdn);

/**
 * Sets a new RDN for a given DN.
 *
 * This function changes the RDN of \c sdn by adding the value from
 * \c rdn to the parent DN of \c sdn.
 *
 * \param sdn The DN that you want to rename.
 * \param rdn The new RDN.
 * \return A pointer to the \c Slapi_DN structure that has been renamed.
 * \see slapi_sdn_get_rdn()
 */
Slapi_DN *slapi_sdn_set_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn);

/**
 * Sets a new parent for a given DN.
 *
 * This function keeps the RDN of the original DN and adds it to the
 * specified parent DN.
 *
 * \param sdn The \c Slapi_DN structure containing the DN whose parent you want to change.
 * \param parentdn The \c Slapi_DN structure containing the new parent DN.
 * \return A pointer to the \c Slapi_DN structure that contains the new DN.
 * \see slapi_sdn_isparent()
 * \see slapi_sdn_get_parent()
 */
Slapi_DN *slapi_sdn_set_parent(Slapi_DN *sdn, const Slapi_DN *parentdn);

/**
 * Builds a new DN out of a new RDN and the DN of the new parent.
 *
 * The new DN is worked out by adding the new RDN in \c newrdn to a
 * parent DN. The parent will be the value in \c newsuperiordn if different
 * from \c NULL, and will otherwise be taken from \c dn_olddn by removing
 * the old RDN (the parent of the entry will still be the same as the new DN).
 *
 * \param dn_olddn The old DN value.
 * \param newrdn The new RDN value.
 * \param newsuperiordn If not \c NULL, will be the DN of the future superior
 *        entry of the new DN, which will be worked out by adding the value
 *        in \c newrdn in front of the content of this parameter.
 * \return The new DN for the entry whose previous DN was \c dn_olddn.
 */
char *slapi_moddn_get_newdn(Slapi_DN *dn_olddn, const char *newrdn, const char *newsuperiordn);
Slapi_DN *slapi_sdn_add_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn);


/*
 * Slapi_RDN functions
 */
/**
 * Creates a new \c Slapi_RDN structure.
 *
 * Allocates the necessary memory and initializes both the
 * RDN value and the array of split RDNs to \c NULL.
 *
 * \return A pointer to the newly allocated, and still empty, \c Slapi_RDN structure.
 * \warning You must free the returned \c Slapi_RDN structure by calling \c slapi_rdn_free()
 *       when you are finished using it.
 * \see slapi_rdn_init()
 * \see slapi_rdn_done()
 * \see slapi_rdn_free()
 */
Slapi_RDN *slapi_rdn_new(void);

/**
 * Creates a new \c Slapi_RDN structure and initializes it from a string.
 *
 * \param dn The DN value whose RDN will be used to initialize the new
 *        \c Slapi_RDN structure.
 * \return A pointer to the newly allocated and initialized \c Slapi_RDN structure.
 * \warning You must free the returned \c Slapi_RDN structure by calling \c slapi_rdn_free()
 *       when you are finished using it.
 * \see slapi_rdn_new_sdn()
 * \see slapi_rdn_new_rdn()
 * \see slapi_rdn_free()
 */
Slapi_RDN *slapi_rdn_new_dn(const char *dn);

/**
 * Creates a new \c Slapi_RDN structure and initializes it from a \c Slapi_DN.
 *
 * \param sdn The \c Slapi_DN structure whose RDN will be used to initialize
 *        the new \c Slapi_RDN structure.
 * \return A pointer to the newly allocated and initialized \c Slapi_RDN structure.
 * \warning You must free the returned \c Slapi_RDN structure by calling \c slapi_rdn_free()
 *       when you are finished using it.
 * \see slapi_rdn_new_dn()
 * \see slapi_rdn_new_rdn()
 * \see slapi_rdn_free()
 */
Slapi_RDN *slapi_rdn_new_sdn(const Slapi_DN *sdn);

/**
 * Creates a new \c Slapi_RDN structure and initializes it from a \c Slapi_RDN.
 *
 * \param fromrdn The \c Slapi_RDN structure whose RDN will be used to initialize
 *        the new \c Slapi_RDN structure.
 * \return A pointer to the newly allocated and initialized \c Slapi_RDN structure.
 * \warning You must free the returned \c Slapi_RDN structure by calling \c slapi_rdn_free()
 *       when you are finished using it.
 * \see slapi_rdn_new_dn()
 * \see slapi_rdn_new_sdn()
 * \see slapi_rdn_free()
 */
Slapi_RDN *slapi_rdn_new_rdn(const Slapi_RDN *fromrdn);

/**
 * Clears out a \c Slapi_RDN structure.
 *
 * Sets both the RDN value and the array of split RDNs to \c NULL.
 *
 * \param rdn The \c Slapi_RDN structure to be initialized.
 * \warning The previous contents of \c rdn are not freed.  It is
 *       up to the caller to do this first if necessary.
 * \see slapi_rdn_new()
 * \see slapi_rdn_free()
 * \see slapi_rdn_done()
 */
void slapi_rdn_init(Slapi_RDN *rdn);

/**
 * Initializes a \c Slapi_RDN structure from a string.
 *
 * \param rdn The \c Slapi_RDN structure to be initialized.
 * \param dn The DN value whose RDN will be used to initialize \c rdn.
 * \warning The previous contents of \c rdn are not freed.  It is
 *       up to the caller to do this first if necessary.
 * \see slapi_rdn_done()
 * \see slapi_rdn_init_sdn()
 * \see slapi_rdn_init_rdn()
 */
void slapi_rdn_init_dn(Slapi_RDN *rdn, const char *dn);

/**
 * Initializes a \c Slapi_RDN structure from a \c Slapi_DN.
 *
 * \param rdn The \c Slapi_RDN structure to be initialized.
 * \param sdn The \c Slapi_DN whose RDN will be used to initialize \c rdn.
 * \warning The previous contents of \c rdn are not freed.  It is
 *       up to the caller to do this first if necessary.
 * \see slapi_rdn_done()
 * \see slapi_rdn_init_dn()
 * \see slapi_rdn_init_rdn()
 */
void slapi_rdn_init_sdn(Slapi_RDN *rdn, const Slapi_DN *sdn);

/**
 * Initializes a \c Slapi_RDN structure from another \c Slapi_RDN.
 *
 * \param rdn The \c Slapi_RDN structure to be initialized.
 * \param fromrdn The \c Slapi_RDN structure that will be used to
 *        initialize \c rdn.
 * \warning The previous contents of \c rdn are not freed.  It is
 *       up to the caller to do this first if necessary.
 * \see slapi_rdn_done()
 * \see slapi_rdn_init_dn()
 * \see slapi_rdn_init_sdn()
 */
void slapi_rdn_init_rdn(Slapi_RDN *rdn, const Slapi_RDN *fromrdn);

/**
 * Sets the RDN value in a \c Slapi_RDN structure from a string.
 *
 * The previous contents of the \c rdn are freed before
 * the new RDN is set.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param dn The DN value whose RDN will be set in \c rdn.
 * \see slapi_rdn_set_sdn()
 * \see slapi_rdn_set_rdn()
 */
void slapi_rdn_set_dn(Slapi_RDN *rdn, const char *dn);
#define SLAPI_RDN_SET_DN_SKIP_UNIQUEID 1
#define SLAPI_RDN_SET_DN_INCLUDE_UNIQUEID 2
void slapi_rdn_set_dn_ext(Slapi_RDN *rdn, const char *dn, int flags);
Slapi_RDN *slapi_rdn_new_all_dn(const char *dn);
int slapi_rdn_init_all_dn(Slapi_RDN *rdn, const char *dn);
int slapi_rdn_init_all_sdn(Slapi_RDN *rdn, const Slapi_DN *sdn);
int slapi_rdn_init_all_sdn_ext(Slapi_RDN *rdn, const Slapi_DN *sdn, int is_tombstone);

/**
 * Sets the RDN value in a \c Slapi_RDN structure from a \c Slapi_DN.
 *
 * The previous contents of the \c rdn are freed before
 * the new RDN is set.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param sdn The \c Slapi_DN value whose RDN will be set in \c rdn.
 * \see slapi_rdn_set_dn()
 * \see slapi_rdn_set_rdn()
 */
void slapi_rdn_set_sdn(Slapi_RDN *rdn, const Slapi_DN *sdn);

/**
 * Sets the RDN value in a \c Slapi_RDN structure from a \c Slapi_RDN.
 *
 * The previous contents of the \c rdn are freed before
 * the new RDN is set.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param fromrdn The \c Slapi_RDN value whose RDN will be set in \c rdn.
 * \see slapi_rdn_set_dn()
 * \see slapi_rdn_set_sdn()
 */
void slapi_rdn_set_rdn(Slapi_RDN *rdn, const Slapi_RDN *fromrdn);

/**
 * Frees a \c Slapi_RDN structure and it's contents from memory.
 *
 * \param rdn A pointer to a pointer of the \c Slapi_RDN strucure to be freed.
 * \see slapi_rdn_new()
 * \see slapi_rdn_done()
 */
void slapi_rdn_free(Slapi_RDN **rdn);

/**
 * Checks if the value of ipAddress is a IPv6 address
 *
 * \param ipAddress is a string that is either an IPv4 or IPv6 address
 * \return 1 if address is an IPv6 address
 * \return 0 if address is an IPv4 address
 */
int slapi_is_ipv6_addr(const char *ipAddress);

/**
 * Returns the length of a ber-encoded ldap operation
 *
 * \param bv is the berval of the encoded ldap operation
 * \param strict set to 0 if "bv" does not contain the entire LDAP encoding
 * \return -1 if the length can not be determined
 * \return length
 */
ber_len_t slapi_berval_get_msg_len(struct berval *bv, int strict);

/**
 * Frees and clears the contents of a \c Slapi_RDN structure from memory.
 *
 * Both the RDN value and the array of split RDNs are freed. Those pointers
 * are then set to \c NULL.
 *
 * \param rdn A pointer to the \c Slapi_RDN strucure to clear.
 * \see slapi_rdn_free()
 * \see slapi_rdn_init()
 */
void slapi_rdn_done(Slapi_RDN *rdn);

/**
 * Gets the first RDN stored in a \c Slapi_RDN structure.
 *
 * The type and the value of the first RDN are retrieved.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param type Address to return a pointer to the type of the first RDN.  If
 *        this is \c NULL at the return of the function, it means that \c rdn
 *        is empty.
 * \param value Address to return a pointer to the value of the first RDN.
 *        If this is \c NULL at the return of the function, it means that
 *        \c rdn is empty.
 * \return \c -1 if \c rdn is empty.
 * \return \c 1 if the operation is successful.
 * \warning Do not free the returned type or value.
 * \see slapi_rdn_get_next()
 * \see slapi_rdn_get_rdn()
 */
int slapi_rdn_get_first(Slapi_RDN *rdn, char **type, char **value);

/**
 * Gets the next RDN stored in a \c Slapi_RDN structure.
 *
 * The type/value pair for the RDN at the position following that indicated
 * by \c index will be retrieved.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param index Indicates the position that precedes that of the desired RDN.
 *        For example, pass 1 if you would like the second RDN.
 * \param type Address to return a pointer to the type of the next RDN.  If
 *        this is \c NULL at the return of the function, it means that \c rdn
 *        is empty.
 * \param value Address to return a pointer to the value of the next RDN.
 *        If this is \c NULL at the return of the function, it means that
 *        \c rdn is empty.
 * \return \c -1 if no RDN exists at the index position.
 * \return The position (\c index + \c 1) of the retrieved RDN if the operation is successful.
 * \see slapi_rdn_get_first()
 * \see slapi_rdn_get_rdn()
 */
int slapi_rdn_get_next(Slapi_RDN *rdn, int index, char **type, char **value);

/**
 * Finds a RDN in a \c Slapi_RDN structure and returns it's index.
 *
 * The \c Slapi_RDN structure will be searched for a RDN matching
 * the given type and value.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param type Type (\c cn, \c o, \c ou, etc.) of the RDN that is searched for.
 * \param value Value of the RDN that is searched for.
 * \param length Gives the length of the value that should be taken into
 *        account for the string comparisons when searching for the RDN.
 * \return The index of the RDN that matches \c type and \c value.
 * \return \c -1 if no RDN stored in \c rdn matches \c type and \c value.
 * \see slapi_rdn_get_index_attr()
 * \see slapi_rdn_contains()
 */
int slapi_rdn_get_index(Slapi_RDN *rdn, const char *type, const char *value, size_t length);

/**
 * Finds a RDN for a given type in a \c Slapi_RDN structure and returns it's index.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param type Type (\c cn, \c o, \c ou, etc.) of the RDN that is searched for.
 * \param value Address to return a pointer to the value of the next RDN.
 *        If this is \c NULL at the return of the function, it means that
 *        no matching RDN exist in \c rdn.
 * \return The index of the RDN that matches \c type.
 * \return \c -1 if no RDN stored in \c rdn matches \c type.
 * \see slapi_rdn_get_index()
 */
int slapi_rdn_get_index_attr(Slapi_RDN *rdn, const char *type, char **value);

/**
 * Checks if a RDN exists in a \c Slapi_RDN structure.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param type Type (\c cn, \c o, \c ou, etc.) of the RDN that is searched for.
 * \param value Value of the RDN that is searched for.
 * \param length Gives the length of the value that should be taken into
 *        account for the string comparisons when searching for the RDN.
 * \return \c 1 if \c rdn contains a RDN that matches \c type and \c value.
 * \return \c 0 if no RDN stored in \c rdn matches \c type and \c value.
 * \see slapi_rdn_get_index()
 * \see slapi_rdn_contains_attr()
 */
int slapi_rdn_contains(Slapi_RDN *rdn, const char *type, const char *value, size_t length);

/**
 * Checks if a RDN for a given type exists in a \c Slapi_RDN structure.
 *
 * \param rdn The \c Slapi_RDN structure containing the RDN value(s).
 * \param type Type (\c cn, \c o, \c ou, etc.) of the RDN that is searched for.
 * \param value Address to return a pointer to the value of the next RDN.
 *        If this is \c NULL at the return of the function, it means that
 *        no matching RDN exist in \c rdn.
 * \return \c 1 if \c rdn contains a RDN that matches \c type.
 * \return \c 0 if no RDN stored in \c rdn matches \c type.
 * \see slapi_rdn_get_index_attr()
 * \see slapi_rdn_contains()
 */
int slapi_rdn_contains_attr(Slapi_RDN *rdn, const char *type, char **value);

/**
 * Adds a new RDN to a \c Slapi_RDN structure.
 *
 * A new type/value pair will be added to an existing RDN, or the type/value
 * pair will be set as the new RDN if \c rdn is empty. This function resets the
 * FLAG_RDNS flags, which means that the RDN array with-in the \c Slapi_RDN
 * structure is no longer current with the new RDN.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param type The type (\c cn, \c o, \c ou, etc.) of the RDN to be added.
 *        This parameter cannot be \c NULL.
 * \param value The value of the RDN to be added. This parameter cannot
 *        be \c NULL.
 * \return Always returns 1.
 * \see slapi_rdn_get_num_components()
 */
int slapi_rdn_add(Slapi_RDN *rdn, const char *type, const char *value);

/**
 * Removes a RDN from a \c Slapi_RDN structure at a given position.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param atindex The index of the RDN type/value pair to remove.
 * \return \c 1 if the RDN is removed from \c rdn.
 * \return \c 0 if no RDN is removed because either \c rdn is empty
 *         or \c atindex goes beyond the number of RDNs present.
 * \see slapi_rdn_remove()
 * \see slapi_rdn_remove_attr()
 */
int slapi_rdn_remove_index(Slapi_RDN *rdn, int atindex);

/**
 * Removes a RDN from a \c Slapi_RDN structure matching a given type/value pair.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param type The type (\c cn, \c o, \c ou, etc.) of the RDN to be removed.
 * \param value The value of the RDN to be removed.
 * \param length Gives the length of the value that should be taken into
 *        account for the string comparisons when searching for the RDN.
 * \return \c 1 if the RDN is removed from \c rdn.
 * \return \c 0 if no RDN is removed.
 * \see slapi_rdn_remove_attr()
 * \see slapi_rdn_remove_index()
 */
int slapi_rdn_remove(Slapi_RDN *rdn, const char *type, const char *value, size_t length);

/**
 * Removes a RDN from a \c Slapi_RDN structure matching a given type.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \param type The type (\c cn, \c o, \c ou, etc.) of the RDN to be removed.
 * \return \c 1 if the RDN is removed from \c rdn.
 * \return \c 0 if no RDN is removed.
 * \see slapi_rdn_remove()
 * \see slapi_rdn_remove_index()
 */
int slapi_rdn_remove_attr(Slapi_RDN *rdn, const char *type);

/**
 * Checks whether a RDN value is stored in a \c Slapi_RDN structure.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \return \c 1 if there is no RDN value present.
 * \return \c 0 if rdn contains a value.
 * \see slapi_rdn_init()
 * \see slapi_rdn_done()
 * \see slapi_rdn_free()
 */
int slapi_rdn_isempty(const Slapi_RDN *rdn);

/**
 * Gets the number of RDN type/value pairs present in a \c Slapi_RDN structure.
 *
 * \param rdn The target \c Slapi_RDN structure.
 * \return The number of RDN type/value pairs present in \c rdn.
 * \see slapi_rdn_add()
 */
int slapi_rdn_get_num_components(Slapi_RDN *rdn);

/**
 * Compares two \c Slapi_RDN structures.
 *
 * For RDNs to be considered equal, the order of their components
 * do not have to be the same.
 *
 * \param rdn1 The first RDN to compare.
 * \param rdn2 The second RDN to compare.
 * \return \c 0 if \c rdn1 and \c rdn2 have the same RDN components.
 * \return \c -1 if they do not have the same components.
 */
int slapi_rdn_compare(Slapi_RDN *rdn1, Slapi_RDN *rdn2);

/**
 * Gets the RDN from a \c Slapi_RDN structure.
 *
 * \param rdn The \c Slapi_RDN structure holding the RDN value.
 * \return The RDN value.
 * \warning Do not free the returned RDN value.
 */
const char *slapi_rdn_get_rdn(const Slapi_RDN *rdn);

/**
 * Adds a RDN from a \c Slapi_RDN structure to a DN in a \c Slapi_DN structure.
 *
 * The RDN in the \c Slapi_RDN structure will be appended to the DN
 * value in \c sdn.
 *
 * \param sdn \c Slapi_DN structure containing the value to which
 *        a new RDN is to be added.
 * \param rdn \c Slapi_RDN structure containing the RDN value
 *        that is to be added to the DN value.
 * \return The \c Slapi_DN structure with the new DN.
 * \see slapi_sdn_set_rdn()
 */
Slapi_DN *slapi_sdn_add_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn);

/* Function:    slapi_rdn_set_all_dn
   Description: this function sets exploded RDNs of DN to Slapi_RDN
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               dn - distinguished name which is to be exploded into RDNs and
                    set to Slapi_RDN
   Return: none
*/
void slapi_rdn_set_all_dn(Slapi_RDN *rdn, const char *dn);

/**
 * Gets the normalized RDN from a \c Slapi_RDN structure
 *
 * \param rdn The \c Slapi_RDN structure holding the RDN value.
 * \return The normalized RDN value.
 */
const char *slapi_rdn_get_nrdn(Slapi_RDN *srdn);

/* Function:    slapi_rdn_get_first_ext
   Description: this function returns the first RDN in RDN array.  The RDN
                array is supposed to store all the RDNs of DN.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               firstrdn - a container to store the address of the first RDN.
                         The caller should not free the returned address.
               flag - type of the returned RDN.  one of the followings:
                      FLAG_ALL_RDNS -- raw (not normalized)
                      FLAG_ALL_NRDNS -- normalized
   Return: the index of the first rdn 0, if function succeeds.
           -1, if it fails.
*/
int slapi_rdn_get_first_ext(Slapi_RDN *srdn, const char **firstrdn, int flag);

/* Function:    slapi_rdn_get_last_ext
   Description: this function returns the last RDN in RDN array.  The RDN
                array is supposed to store all the RDNs of DN.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               lastrdn - a container to store the address of the last RDN.
                         The caller should not free the returned address.
               flag - type of the returned RDN.  one of the followings:
                      FLAG_ALL_RDNS -- raw (not normalized)
                      FLAG_ALL_NRDNS -- normalized
   Return: the index of the last rdn, if function succeeds.
           -1, if it fails.
*/
int slapi_rdn_get_last_ext(Slapi_RDN *srdn, const char **lastrdn, int flag);
/* Function:    slapi_rdn_get_prev_ext
   Description: this function returns the previous RDN of the given index (idx)
                in RDN array.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               idx - a return value of the previous slapi_rdn_get_last_ext
                     or slapi_rdn_get_prev_ext or slapi_rdn_get_next_ext call.
               prevrdn - a container to store the address of the previous RDN.
                         The caller should not free the returned address.
               flag - type of the returned RDN.  one of the followings:
                      FLAG_ALL_RDNS -- raw (not normalized)
                      FLAG_ALL_NRDNS -- normalized
   Return: the index of the returned rdn, if function succeeds.
           -1, if it fails.
*/
int slapi_rdn_get_prev_ext(Slapi_RDN *srdn, int idx, const char **prevrdn, int flag);
/* Function:    slapi_rdn_get_next_ext
   Description: this function returns the next RDN of the given index (idx)
                in RDN array.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               idx - a return value of the previous slapi_rdn_get_prev_ext
                     or slapi_rdn_get_next_ext call.
               nextrdn - a container to store the address of the next RDN.
                         The caller should not free the returned address.
               flag - type of the returned RDN.  one of the followings:
                      FLAG_ALL_RDNS -- raw (not normalized)
                      FLAG_ALL_NRDNS -- normalized
   Return: the index of the returned rdn, if function succeeds.
           -1, if it fails.
*/
int slapi_rdn_get_next_ext(Slapi_RDN *srdn, int idx, const char **nextrdn, int flag);
/* Function:    slapi_rdn_add_rdn_to_all_rdns
   Description: this function appends the given RDN to the RDN array in
                Slapi_RDN.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               addrdn - an RDN string to append.
               byref - non 0, then the passed addrdn is put in the RDN array.
                       0, then the duplicated addrdn is put in the RDN array.
   Return: 0, if the function succeeds.
           -1, if it fails.
*/
int slapi_rdn_add_rdn_to_all_rdns(Slapi_RDN *srdn, char *addrdn, int byref);
/* Function:    slapi_rdn_add_srdn_to_all_rdns
   Description: this function appends the given Slapi_RDN to the RDN array in
                Slapi_RDN.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               addsrdn - Slapi_RDN to append.
   Return: 0, if the function succeeds.
           -1, if it fails.
*/
int slapi_rdn_add_srdn_to_all_rdns(Slapi_RDN *srdn, Slapi_RDN *addsrdn);
/* Function:    slapi_rdn_get_dn
   Description: this function generates DN string if it stores RDNs in its
                all_rdns field.
   Parameters: srdn - a pointer to Slapi_RDN which stores RDN array
               dn -- a container to store the address of DN; dn is allocated
                     in this function.
   Return: 0, if the function succeeds.
           -1, if it fails (e.g., srdn is NULL, dn is NULL, or srdn does not
               have RDN array in it.
*/
int slapi_rdn_get_dn(Slapi_RDN *srdn, char **dn);
/* Function:    slapi_srdn_copy
   Description: this function copies "from" Slapi_RDN structure to "to"
   Parameters: from - a pointer to source Slapi_RDN
               to -- an address to store the copied Slapi_RDN.
   Return: 0, if the function succeeds.
           -1, if it fails
*/
int slapi_srdn_copy(const Slapi_RDN *from, Slapi_RDN *to);
/* Function:    slapi_srdn_copy
   Description: this function replaces the rdn in Slapi_RDN with new_rdn
   Parameters: srdn - a pointer to Slapi_RDN
               new_rdn -- new rdn set to Slapi_RDN
   Return: 0, if the function succeeds.
           -1, if it fails
*/
int slapi_rdn_replace_rdn(Slapi_RDN *srdn, char *new_rdn);
/* Function:    slapi_rdn_partial_dup
   Description: this function partially duplicates "from" Slapi_RDN structure and set it to "to"
   Parameters: from - a pointer to source Slapi_RDN
               to -- an address to store the partially copied Slapi_RDN.
               idx -- index from which the duplicate begins
   Return: 0, if the function succeeds.
           -1, if it fails
*/
int slapi_rdn_partial_dup(Slapi_RDN *from, Slapi_RDN **to, int idx);

/**
 * Return the size of the RDN
 *
 * \param srdn A pointer to Slapi_RDN to calculate the size
 * \return The size of the given RDN.
 */
size_t slapi_rdn_get_size(Slapi_RDN *srdn);

/**
 * Return a copy of the value of the RDN
 *
 * \param rdn A pointer to rdn to get the value.
 * \return The value of the given RDN.
 */
char *slapi_rdn_get_value(const char *rdn);

/**
 * Return a pointer to the value of the RDN
 *
 * \param rdn A pointer to rdn to get the value.
 * \return The value of the given RDN.
 */
char *slapi_rdn_get_value_by_ref(const char *rdn);

/**
 * Check if the rdn is multivalued or not
 *
 * \param rdn A pointer to rdn to exam.
 * \return 1, if the rdn is multi valued.
 *         0, if the rdn is simgle valued.
 */
int slapi_rdn_is_multivalued(Slapi_RDN *rdn);

/**
 * Check if the rdn is a conflict rdn or not
 *
 * \param rdn A pointer to rdn to exam.
 * \return 1, if the rdn is a conflict rdn
 *         0, if the rdn is not a conflict rdn
 */
int slapi_rdn_is_conflict(Slapi_RDN *rdn);

/*
 * utility routines for dealing with DNs
 */
/**
 * Does nothing. (DEPRECATED)
 *
 * \param dn The DN to normalize.
 * \return The normalized DN.
 * \deprecated Use slapi_dn_normalized_ext.
 */
char *slapi_dn_normalize(char *dn);

/**
 * Does nothing. (DEPRECATED)
 *
 * \param dn The DN value to normalize.
 * \param end Pointer to the end of what will be normalized from the DN
 *        value in \c dn.  If this parameter is \c NULL, the DN value
 *        will be wholly normalized.
 * \return The normalized DN.
 * \deprecated Use slapi_dn_normalized_ext.
 */
char *slapi_dn_normalize_to_end(char *dn, char *end);

/**
 * Normalizes a DN.
 *
 * \param src The DN to normalize.
 * \param src_len The length of src DN to normalize. If 0 is given, strlen(src) is used.
 * \param dest The normalized DN.
 * \param dest The length of the normalized DN dest.
 * \return \c 0 if successful. The dest DN is normalized in line. Caller must not free dest.
 * \return \c 1 if successful. The dest DN is allocated.  Caller must free dest.
 * \return \c -1 if an error occurs (for example, if the src DN cannot be normalized)
 */
int slapi_dn_normalize_ext(char *src, size_t src_len, char **dest, size_t *dest_len);

/**
 * Normalizes a DN (in lower-case characters).
 *
 * \param src The DN to normalize.
 * \param src_len The length of src DN to normalize. If 0 is given, strlen(src) is used internally.
 * \param dest The normalized DN with the cases lowered.
 * \param dest_len The length of the normalized DN dest.
 * \return \c 0 if successful. The dest DN is normalized in line. Caller must not free dest.  The string is NOT NULL terminated.
 * \return \c 1 if successful. The dest DN is allocated.  Caller must free dest.
 * \return \c -1 if an error occurs (for example, if src DN cannot be normalized)
 */
int slapi_dn_normalize_case_ext(char *src, size_t src_len, char **dest, size_t *dest_len);

/**
 * Generate a valid DN string.
 *
 * \param fmt The format used to generate a DN string.
 * \param ... The arguments to generate a DN string.
 * \return A pointer to the generated DN.
 * \return NULL if failed.
 * \note When a DN needs to be internally created, this function is supposed to be called.  This function allocates the enough memory for the normalized DN and returns it filled with the normalized DN.
 */
char *slapi_create_dn_string(const char *fmt, ...);

/**
 * Generates a valid DN string (in lower-case characters).
 *
 * \param fmt The format used to generate a DN string.
 * \param ... The arguments to generate a DN string.
 * \return A pointer to the generated DN.
 * \return NULL if failed.
 */
char *slapi_create_dn_string_case(const char *fmt, ...);

/**
 * Generates a valid value of RDN string
 *
 * \param fmt The format used to generate a value of RDN string.
 * \param ... The arguments to generate an RDN string.
 * \return A pointer to the generated RDN value.
 * \return NULL if failed.
 */
char *slapi_create_rdn_value(const char *fmt, ...);

/**
 * Converts a DN to lowercase.
 *
 * \param dn The DN to convert.
 * \return A pointer to the converted DN.
 * \deprecated Use slapi_sdn_get_ndn() instead to normalize the case,
 *             or use slapi_sdn_compare() to compare regardless of case.
 */
char *slapi_dn_ignore_case(char *dn);

/**
 * Converts a DN to canonical format and converts all characters to lowercase.
 *
 * \param dn DN that you want to normalize and convert to lowercase.
 * \return The normalized and converted DN.
 * \note The \c dn parameter is converted in place.
 */
char *slapi_dn_normalize_case(char *dn);

/**
 * Get a copy of the parent DN of a DN.
 *
 * \param pb A parameter block with the backend set.
 * \param dn The DN whose parent is desired.
 * \return A pointer to the parent DN of \c dn.
 * \return \c NULL if the DN is a suffix of the backend.
 * \warning The caller must free the returned DN when finished with it.
 * \deprecated Use slapi_sdn_get_backend_parent() instead.
 */
char *slapi_dn_beparent(Slapi_PBlock *pb, const char *dn);

/**
 * Finds the parent DN of a DN within the same string.
 *
 * \param dn The DN whose parent DN is desired.
 * \return A pointer to the parent DN within \c dn.
 */
const char *slapi_dn_find_parent(const char *dn);

/**
 * Finds the parent DN of a DN within the same string.
 *
 * \param dn The DN whose parent DN is desired.
 * \param is_tombstone If non-zero, the special leaf rdn "nsuniqueid=.." is ignored.
 * \return A pointer to the parent DN within \c dn.
 */
const char *slapi_dn_find_parent_ext(const char *dn, int is_tombstone);

/**
 * Gets the parent DN of a given DN.
 *
 * \param dn The DN whose parent is desired.
 * \return A pointer to the parent DN of \c dn.
 * \warning The caller must free the returned DN when finished with it.
 * \deprecated Use slapi_sdn_get_parent() instead.
 */
char *slapi_dn_parent(const char *dn);

/**
 * Gets the parent DN of a given DN.
 *
 * \param dn The DN whose parent is desired.
 * \param is_tombstone If non-zero, the special leaf rdn "nsuniqueid=.." is ignored.
 * \return A pointer to the parent DN of \c dn.
 * \warning The caller must free the returned DN when finished with it.
 * \deprecated Use slapi_sdn_get_parent() instead.
 */
char *slapi_dn_parent_ext(const char *dn, int is_tombstone);

/**
 * Checks if a DN belongs to a suffix.
 *
 * \param dn The DN to check.
 * \param suffix The suffix to check.
 * \return \c 1 if \c dn belongs to \c suffix
 * \return \c 0 if \c dn does not belong to \c suffix.
 * \warning Both \c dn and \c suffix must be normalized before calling this function.
 * \deprecated Use slapi_sdn_issuffix() instead.
 */
int slapi_dn_issuffix(const char *dn, const char *suffix);

/**
 * Checks if a DN is the parent of another DN.
 *
 * \param parentdn The DN of the supposed parent.
 * \param childdn The DN of the supposed child.
 * \return non-zero if \c parentdn is the parent of \c childdn.
 * \return \c 0 if \c parentdn is not the paret of \c childdn.
 * \deprecated Use slapi_sdn_isparent() instead.
 */
int slapi_dn_isparent(const char *parentdn, const char *childdn);

/**
 * Determines is a DN is the root DN.
 *
 * \param dn The DN to check
 * \return \c 1 if the DN is the root DN.
 * \return \c 0 if the DN is not the root DN.
 * \warning You must normalize \c dn before calling this function.
 */
int slapi_dn_isroot(const char *dn);

/**
 * Determines if an SDN is the root DN.
 *
 * \param sdn The DN to check
 * \return \c 1 if the DN is the root DN.
 * \return \c 0 if the DN is not the root DN.
 */
int32_t slapi_sdn_isroot(const Slapi_DN *sdn);

/**
 * Checks if a DN is the backend suffix.
 *
 * \param pb A parameter block with the backend set.
 * \param dn The DN to check.
 * \return \c 1 if \c dn is the backend suffix.
 * \return \c 0 if \c dn is not the backend suffix.
 * \deprecated slapi_be_issuffix()
 */
int slapi_dn_isbesuffix(Slapi_PBlock *pb, const char *dn);

/**
 * Checks if writes to a particular DN need to send a referral.
 *
 * \param target_sdn The target DN that you want to check.
 * \param referral The address of a pointer to receive a referral
 *        if one is needed.
 * \return \c 1 if a referral needs to be sent.
 * \return \c 0 if no referral is needed.
 * \warning The referral entry must be freed when it is no longer
 *          being used.
 */
int slapi_dn_write_needs_referral(Slapi_DN *target_sdn, Slapi_Entry **referral);

/**
 * Converts the second RDN type value to the berval value.
 *
 * Returns the new RDN value as a berval value in \c bv.  This function
 * can be used for creating the RDN as an attribute value since it returns
 * the value of the RDN in the berval structure.
 *
 * \param rdn Second RDN value.
 * \param type Address of a pointer to receive the attribute type
 *        of the second RDN.
 * \param bv A pointer to the berval structure to receive the value.
 */
int slapi_rdn2typeval(char *rdn, char **type, struct berval *bv);

/**
 * Adds a RDN to a DN.
 *
 * \param dn The DN to add the RDN to.
 * \param rdn the new RDN to add to the DN.
 * \return A pointer to the new DN.
 * \warning The caller must free the returnd DN when finished with it.
 * \deprecated Use slapi_sdn_add_rdn() instead.
 */
char *slapi_dn_plus_rdn(const char *dn, const char *rdn);

/**
 * Create an Slapi_DN that is a common ancestor of given 2 Slapi_DN's
 *
 * \param dn1 The first DN
 * \param dn2 the second DN
 * \return A pointer to the new Slapi_DN that is a common ancestor of dn1 and dn2
 * \warning The caller must free the returnd Slapi_DN
 */
Slapi_DN *slapi_sdn_common_ancestor(Slapi_DN *dn1, Slapi_DN *dn2);

/*
 * thread safe random functions
 */
/**
 * Generate a pseudo-random integer with optional seed.
 *
 * \param seed A seed to use when generating the pseudo random number.
 * \return A pseudo random number.
 * \see slapi_rand()
 * \see slapi_rand_array()
 */
int slapi_rand_r(unsigned int *seed);

/* Generate a pseudo-random integer in an array.
 *
 * \param randx The array you want filled with the random number.
 * \param len The length of the array you want filled with the random number.
 * \see slapi_rand()
 * \see slapi_rand_r()
 */
void slapi_rand_array(void *randx, size_t len);

/**
 * Generate a pseudo-random integer.
 *
 * \return A pseudo random number.
 * \see slapi_rand_r()
 * \see slapi_rand_array()
 */
int slapi_rand(void);

/**
 * Escape special characters in a search filter value
 *
 * \param filter_str A ldap search filter value (cn=VALUE_TO_BE_ESCAPED)
 * \param len length of the search filter value.  -1 indicates the string is null terminated
 * \param buf the buffer to store the escaped filter value string
 * \return the escaped filter value string
 */
char *slapi_escape_filter_value(char *filter_str, int len);

#define ESC_NEXT_VAL "__ESC__NEXT__VAL__"
#define NORM_NEXT_VAL "__NORM__NEXT__VAL__"
#define ESC_AND_NORM_NEXT_VAL "__ESC__AND__NORM__NEXT__VAL__"

char *slapi_filter_sprintf(const char *fmt, ...);

/*
 * attribute routines
 */
/**
 * Create a new empty attribute.
 *
 * \return A pointer to the newly created attribute.
 * \warning You must free the returned attribute using slapi_attr_free().
 * \see slapi_attr_init()
 * \see slapi_attr_dup()
 * \see slapi_attr_free()
 */
Slapi_Attr *slapi_attr_new(void);

/**
 * Initializes an attribute with an attribute type.
 *
 * \param a The attribute to initialize.
 * \param type The attribute type to set.
 * \return A pointer to the initialized attribute.
 */
Slapi_Attr *slapi_attr_init(Slapi_Attr *a, const char *type);

/**
 * Frees an attribute from memory.
 *
 * \param a Address of a pointer to the attribute to be freed.
 * \see slapi_attr_init()
 * \see slapi_attr_dup()
 * \see slapi_attr_new()
 */
void slapi_attr_free(Slapi_Attr **a);

/**
 * Make a copy of an attribute.
 *
 * \param attr The attribute to be duplicated.
 * \return The newly created copy of the attribute.
 * \warning You must free the returned attribute using slapi_attr_free().
 * \see slapi_attr_new()
 * \see slapi_attr_init()
 * \see slapi_attr_free()
 */
Slapi_Attr *slapi_attr_dup(const Slapi_Attr *attr);

/**
 * Adds a value to an attribute.
 *
 * \param a The attribute that will contain the values.
 * \param v Value to be added to the attribute.
 * \return Always returns 0.
 * \see slapi_attr_first_value()
 * \see slapi_attr_next_value()
 * \see slapi_attr_get_numvalues()
 * \see slapi_attr_value_cmp()
 * \see slapi_attr_value_find()
 */
int slapi_attr_add_value(Slapi_Attr *a, const Slapi_Value *v);

/**
 * Find syntax plugin associated with an attribute type.
 *
 * \param type Type of attribute for which you want to get the plugin.
 * \param pi Address to receive a pointer to the plugin structure.
 * \return \c 0 if successful.
 * \return \c -1 if the plugin is not found.
 * \deprecated This function was necessary in order to call syntax plugin
 *             filter and indexing functions - there are new functions
 *             to use instead, such as slapi_attr_values2keys, etc.
 *             This function is still used by internal APIs, but new
 *             code should not use this function
 * \see slapi_attr_get_type()
 * \see slapi_attr_type_cmp()
 * \see slapi_attr_types_equivalent()
 * \see slapi_attr_basetype()
 */
int slapi_attr_type2plugin(const char *type, void **pi);

/**
 * Get the name of the attribute type from a specified attribute.
 *
 * \param attr Attribute for which you want to get the type.
 * \param type Address to receive a pointer to the attribute type.
 * \return Always returns \c 0.
 * \warning Do not free the returned attribute type.  The type is a part
 *       if the actual attribute data, not a copy.
 * \see slapi_attr_type2plugin()
 * \see slapi_attr_type_cmp()
 * \see slapi_attr_types_equivalent()
 * \see slapi_attr_basetype()
 */
int slapi_attr_get_type(Slapi_Attr *attr, char **type);

/**
 * Get the attribute type OID of a particular attribute.
 *
 * \param attr Attribute that contains the desired OID.
 * \param oidp Address to receive a pointer to a copy of the
 *        attribute type OID.
 * \return \c 0 if the attribute type is found.
 * \return \c -1 if the attribute type is not found.
 * \warning The returned OID should be freed by calling the
 *       slapi_ch_free_string() function.
 * \see slapi_attr_get_syntax_oid_copy()
 */
int slapi_attr_get_oid_copy(const Slapi_Attr *attr, char **oidp);

/*
 * Get the syntax OID of a particular attribute.
 *
 * \param a Attribute that contains the desired OID.
 * \param oidp Address to receive a pointer to a copy of the
 *        syntax OID.
 * \return \c 0 if the syntax OID is found.
 * \return \c -1 if the syntax OID is not found.
 * \warning The returned OID should be freed by calling the
 *       slapi_ch_free_string() function.
 * \see slapi_attr_get_oid_copy()
 */
int slapi_attr_get_syntax_oid_copy(const Slapi_Attr *a, char **oidp);

/**
 * Checks if the attribute uses a DN syntax or not.
 *
 * \param attr The attribute to be checked.
 * \return \c non 0 if the attribute uses a DN syntax.
 * \return \c 0 if the attribute does not use a DN syntax.
 */
int slapi_attr_is_dn_syntax_attr(Slapi_Attr *attr);
int slapi_attr_is_dn_syntax_type(char *type);

/**
 * Get the flags associated with a particular attribute.
 *
 * Valid flags are:
 *     \arg #SLAPI_ATTR_FLAG_SINGLE
 *     \arg #SLAPI_ATTR_FLAG_OPATTR
 *     \arg #SLAPI_ATTR_FLAG_READONLY
 *     \arg #SLAPI_ATTR_FLAG_OBSOLETE
 *     \arg #SLAPI_ATTR_FLAG_COLLECTIVE
 *     \arg #SLAPI_ATTR_FLAG_NOUSERMOD
 *     \arg #SLAPI_ATTR_FLAG_NORMALIZED
 *     \arg #SLAPI_ATTR_FLAG_NOEXPOSE
 *
 * \param attr Attribute for which you want to get the flags.
 * \param flags Address of an integer that you want to reveive the flags.
 * \return \c Always returns 0.
 * \see slapi_attr_flag_is_set()
 */
int slapi_attr_get_flags(const Slapi_Attr *attr, unsigned long *flags);

/**
 * Checks if certain flags are set for a particular attribute.
 *
 * Valid flags are:
 *     \arg #SLAPI_ATTR_FLAG_SINGLE
 *     \arg #SLAPI_ATTR_FLAG_OPATTR
 *     \arg #SLAPI_ATTR_FLAG_READONLY
 *     \arg #SLAPI_ATTR_FLAG_OBSOLETE
 *     \arg #SLAPI_ATTR_FLAG_COLLECTIVE
 *     \arg #SLAPI_ATTR_FLAG_NOUSERMOD
 *     \arg #SLAPI_ATTR_FLAG_NORMALIZED
 *     \arg #SLAPI_ATTR_FLAG_NOEXPOSE
 *
 * \param attr Attribute that you want to check.
 * \param flag Flags to check in the attribute.
 * \return \c 1 if the specified flags are set.
 * \return \c 0 if the specified flags are not set.
 * \see slapi_attr_get_flags()
 */
int slapi_attr_flag_is_set(const Slapi_Attr *attr, unsigned long flag);

/**
 * Comare two bervals for a given attribute.
 *
 * \param attr Attribute used to determine how these values are compared; for
 *        example, the syntax of the attribute may perform case-insensitive
 *        comparisons.
 * \param v1 Pointer to the \c berval structure containing the first value
 *        that you want to compare.
 * \param v2 Pointer to the \c berval structure containing the second value
 *        that you want to compare.
 * \return \c 0 if the values are equal.
 * \return \c -1 if the values are not equal.
 * \see slapi_attr_add_value()
 * \see slapi_attr_first_value()
 * \see slapi_attr_next_value()
 * \see slapi_attr_get_numvalues()
 * \see slapi_attr_value_find()
 */
int slapi_attr_value_cmp(const Slapi_Attr *attr, const struct berval *v1, const struct berval *v2);

/**
 * Comare two values for a given attribute.
 *
 * \param attr Attribute used to determine how these values are compared; for
 *        example, the syntax of the attribute may perform case-insensitive
 *        comparisons.
 * \param v1 Pointer to the \c Slapi_Value structure containing the first value
 *        that you want to compare.
 * \param v2 Pointer to the \c Slapi_Value structure containing the second value
 *        that you want to compare.
 * \return \c 0 if the values are equal.
 * \return \c -1 if the values are not equal.
 * \see slapi_attr_add_value()
 * \see slapi_attr_first_value()
 * \see slapi_attr_next_value()
 * \see slapi_attr_get_numvalues()
 * \see slapi_attr_value_find()
 * \see slapi_attr_value_cmp()
 */
int slapi_attr_value_cmp_ext(const Slapi_Attr *a, Slapi_Value *v1, Slapi_Value *v2);

/**
 * Determine if an attribute contains a given value.
 *
 * \param a Attribute that you want to check.
 * \param v Pointer to the \c berval structure containing the value for
 *          which you want to search.
 * \return \c 0 if the attribute contains the specified value.
 * \return \c -1 if the attribute does not contain the specified value.
 * \see slapi_attr_add_value()
 * \see slapi_attr_first_value()
 * \see slapi_attr_next_value()
 * \see slapi_attr_get_numvalues()
 * \see slapi_attr_value_cmp()
 */
int slapi_attr_value_find(const Slapi_Attr *a, const struct berval *v);

/**
 * Compare two attribute types.
 *
 * \param t1 Name of the first attribute type to compare.
 * \param t2 Name of the second attribute type to compare.
 * \param opt One of the following options:
 *        \arg #SLAPI_TYPE_CMP_EXACT
 *        \arg #SLAPI_TYPE_CMP_BASE
 *        \arg #SLAPI_TYPE_CMP_SUBTYPE
 *        \arg #SLAPI_TYPE_CMP_SUBTYPES
 * \return \c 0 if the type names are equal.
 * \return A non-zero value if the type names are not equal.
 * \see slapi_attr_type2plugin()
 * \see slapi_attr_get_type()
 * \see slapi_attr_types_equivalent()
 * \see slapi_attr_basetype()
 */
int slapi_attr_type_cmp(const char *t1, const char *t2, int opt);

/* Mode of operation (opt) values for slapi_attr_type_cmp() */
/**
 * Compare the types as-is.
 *
 * \see slapi_attr_type_cmp()
 */
#define SLAPI_TYPE_CMP_EXACT 0

/**
 * Compare only the base names of the types.
 *
 * \see slapi_attr_type_cmp()
 */
#define SLAPI_TYPE_CMP_BASE 1

/**
 * Ignore any subtypes in the second type that are not in the first subtype.
 *
 * \see slapi_attr_type_cmp()
 */
#define SLAPI_TYPE_CMP_SUBTYPE 2

/**
 * Compare types including subtypes in the both args.
 *
 * \see slapi_attr_type_cmp()
 */
#define SLAPI_TYPE_CMP_SUBTYPES 3

/**
 * Compare two attribute names to determine if they represent the same value.
 *
 * \param t1 Pointer to the first attribute you want to compare.
 * \param t2 Pointer to the second attribute you want to compare.
 * \return \c 1 if \c t1 and \c t2 represent the same attribute.
 * \return \c 0 if \c t1 and \c t2 do not represent the same attribute.
 * \see slapi_attr_type_cmp()
 * \see slapi_attr_get_type()
 * \see slapi_attr_basetype()
 */
int slapi_attr_types_equivalent(const char *t1, const char *t2);

/**
 * Get the base type of an attribute.
 *
 * For example, if given \c cn;lang-jp, returns \c cn.
 *
 * \param type Attribute type from which you want to get the base type.
 * \param buf Buffer to hold the returned base type.
 * \param bufsiz Size of the buffer.
 * \return \c NULL if the base type fits in the buffer.
 * \return A pointer to a newly allocated base type if the buffer is
 *         too small to hold it.
 * \warning If a base type is returned, if should be freed by calling
 *          slapi_ch_free_string().
 * \see slapi_attr_get_type()
 * \see slapi_attr_type_cmp()
 * \see slapi_attr_types_equivalent()
 */
char *slapi_attr_basetype(const char *type, char *buf, size_t bufsiz);

/**
 * Get the first value of an attribute.
 *
 * This is part of a set of functions to enumerate over a
 * \c Slapi_Attr structure.
 *
 * \param a Attribute containing the desired value.
 * \param v Holds the first value of the attribute.
 * \return \c 0, which is the index of the first value.
 * \return \c -1 if \c NULL or if the value is not found.
 * \warning Do not free the returned value.  It is a part
 *          of the attribute structure and not a copy.
 * \see slapi_attr_next_value()
 * \see slapi_attr_get_num_values()
 */
int slapi_attr_first_value(Slapi_Attr *a, Slapi_Value **v);

/**
 * Get the next value of an attribute.
 *
 * The value of an attribute associated with an index is placed into
 * a value.  This is pare of a set of functions to enumerate over a
 * \c Slapi_Attr structure.
 *
 * \param a Attribute containing the desired value.
 * \param hint Index of the value to be returned.
 * \param v Holds the value of the attribute.
 * \return \c hint plus \c 1 if the value is found.
 * \return \c -1 if \c NULL or if a value at \c hint is not found.
 * \warning Do not free the returned value.  It is a part
 *          of the attribute structure and not a copy.
 * \see slapi_attr_first_value()
 * \see slapi_attr_get_num_values()
 */
int slapi_attr_next_value(Slapi_Attr *a, int hint, Slapi_Value **v);

/**
 * Get the number of values present in an attribute.
 *
 * Counts the number of values in an attribute and places that
 * count in an integer.
 *
 * \param a Attribute containing the values to be counted.
 * \param numValues Integer to hold the counted values.
 * \see slapi_attr_first_value()
 * \see slapi_attr_next_value()
 */
int slapi_attr_get_numvalues(const Slapi_Attr *a, int *numValues);

/**
 * Copy existing values contained in an attribute into a valueset.
 *
 * \param a Attribute containing the values to be placed into
 *        a valueset.
 * \param vs Receives the values from the attribute.
 * \return Always returns \c 0.
 * \warning Free the returned valueset with slapi_valueset_free()
 *          when finished using it.
 * \see slapi_entry_add_valueset()
 * \see slapi_valueset_new()
 * \see slapi_valueset_free()
 * \see slapi_valueset_init()
 * \see slapi_valueset_done()
 * \see slapi_valueset_add_value()
 * \see slapi_valueset_first_value()
 * \see slapi_valueset_next_value()
 * \see slapi_valueset_count()
 */
int slapi_attr_get_valueset(const Slapi_Attr *a, Slapi_ValueSet **vs);

/**
 * Sets the valueset in an attribute.
 *
 * Intializes a valueset in a \c Slapi_Attr structure from a specified
 * \c Slapi_ValueSet structure.  The valueset in the \c Slapi_Attr
 * will be \c vs, not a copy.
 *
 * \param a The attribute to set the valueset in.
 * \param vs The valueset that you want to set in the attribute.
 * \return Always returns \c 0.
 * \warning Do not free \c vs.  Ownership of \c vs is tranferred to
 *          the attribute.
 * \see slapi_valueset_set_valueset()
 */
int slapi_attr_set_valueset(Slapi_Attr *a, const Slapi_ValueSet *vs);

/**
 * Set the attribute type of an attribute.
 *
 * \param a The attribute whose type you want to set.
 * \param type The attribute type you want to set.
 * \return \c 0 if the type was set.
 * \return \c -1 if the type was not set.
 * \warning The passed in type is copied, so ownership of \c type
 *          remains with the caller.
 * \see slapi_attr_get_type()
 */
int slapi_attr_set_type(Slapi_Attr *a, const char *type);

/**
 * Copy the values from an attribute into a berval array.
 *
 * \param a Attribute that contains the desired values.
 * \param vals Pointer to an array of berval structure pointers to
 *        hold the desired values.
 * \return \c 0 if values are found.
 * \return \c -1 if \c NULL.
 * \warning You should free the array using ber_bvecfree() from the
 *          Mozilla LDAP C SDK.
 */
int slapi_attr_get_bervals_copy(Slapi_Attr *a, struct berval ***vals);

/**
 * Normalize an attribute type.
 *
 * The attribute type will be looked up in the defined syntaxes to
 * get the normalized form.  If it is not found, the passed in type
 * will be normalized.
 *
 * \param s The attribute type that you want to normalize.
 * \return A normalized copy of the passed in attribute type.
 * \warning You should free the returned string using slapi_ch_free_string().
 * \see slapi_ch_free_string()
 */
char *slapi_attr_syntax_normalize(const char *s);

/**
 * Normalize an attribute type.
 *
 * The attribute type will be looked up in the defined syntaxes to
 * get the normalized form.  If it is not found, the passed in type
 * will be normalized.  If ATTR_SYNTAX_NORM_ORIG_ATTR is set to flags,
 * the upper and lower cases are kept but trailing spaces are chopped
 * from the original attribute type.
 *
 * \param s The attribute type that you want to normalize.
 * \param flags 0 or ATTR_SYNTAX_NORM_ORIG_ATTR
 * \return A normalized copy of the passed in attribute type.
 * \warning You should free the returned string using slapi_ch_free_string().
 * \see slapi_ch_free_string()
 */
char *slapi_attr_syntax_normalize_ext(char *s, int flags);


/*
 * value routines
 */
/**
 * Create a new empty \c Slapi_Value structure.
 *
 * \return A pointer to the newly allocated \c Slapi_Value structure.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \see slapi_value_free()
 * \see slapi_value_dup()
 * \see slapi_value_new_berval()
 * \see slapi_value_new_string()
 * \see slapi_value_new_string_passin()
 */
Slapi_Value *slapi_value_new(void);

/**
 * Create a new \c Slapi_value structure and initialize it's value.
 *
 * \param bval Pointer to the \c berval structure used to initialize
 *        the newly allocated \c Slapi_value.
 * \return A pointer to the newly allocated and initialized value.
 * \warning The passed in \c berval structure will be copied.  Ownership
 *          of \c bval remains with the caller.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \see slapi_value_free()
 * \see slapi_value_new()
 * \see slapi_value_new_string()
 * \see slapi_value_new_string_passin()
 * \see slapi_value_dup()
 */
Slapi_Value *slapi_value_new_berval(const struct berval *bval);

/**
 * Duplicate a \c Slapi_Value structure.
 *
 * \param v The value to duplicate.
 * \return A pointer to the copy of the value.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \warning This function is identical to slapi_value_dup().
 * \see slapi_value_dup()
 * \see slapi_value_free()
 */
Slapi_Value *slapi_value_new_value(const Slapi_Value *v);

/**
 * Create a new \c Slapi_value structure and initialize it's value.
 *
 * \param s A \c NULL terminated string used to initialize
 *        the newly allocated \c Slapi_value.
 * \return A pointer to the newly allocated and initialized value.
 * \warning The passed in string will be copied.  Ownership of \c s
 *          remains with the caller.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \see slapi_value_free()
 * \see slapi_value_new()
 * \see slapi_value_new_berval()
 \see slapi_value_new_string_passin()
 * \see slapi_value_dup()
 */
Slapi_Value *slapi_value_new_string(const char *s);

/**
 * Create a new \c Slapi_value structure and initialize it's value.
 *
 * \param s A \c NULL terminated string used to initialize
 *        the newly allocated \c Slapi_value.
 * \return A pointer to the newly allocated and initialized value.
 * \warning The passed in string will be used directly as the value.
 *          It will not be copied.  Ownership of \c s is transferred
 *          to the new \c Slapi_Value structure, so it should not be
 *          freed by the caller.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \see slapi_value_free()
 * \see slapi_value_new()
 * \see slapi_value_new_berval()
 * \see slapi_value_new_string()
 * \see slapi_value_dup()
 */
Slapi_Value *slapi_value_new_string_passin(char *s);

/**
 * Initialize a \c Slapi_Value structure.
 *
 * All fields of the passed in \c Slapi_Value will be reset to zero.
 *
 * \param v The value to initialize.
 * \return A pointer to the initialized value.
 * \warning The passed in value must not be \c NULL.
 * \see slapi_value_init_berval()
 * \see slapi_value_init_string()
 * \see slapi_value_init_string_passin()
 */
Slapi_Value *slapi_value_init(Slapi_Value *v);

/**
 * Initialize a \c Slapi_Value structure from the value contained in a \c berval structure.
 *
 * \param v The value to initialize.
 * \param bval The \c berval structure to be used to intialize the value.
 * \return A pointer to the initialized value.
 * \warning The passed in \c Slapi_Value must not be \c NULL.
 * \warning The content of the \c berval structure is duplicated.  It is up
 *          to the caller to manage the memory used by the \c berval.
 * \see slapi_value_init()
 * \see slapi_value_init_string()
 * \see slapi_value_init_string_passin()
 */
Slapi_Value *slapi_value_init_berval(Slapi_Value *v, struct berval *bval);

/**
 * Initialize a \c Slapi_Value with a copy of the value contained in a string.
 *
 * \param v The value to initialize.
 * \param s The null-terminated string to be used to initialize the value.
 * \return A pointer to the initialized value.
 * \warning The passed in \c Slapi_Value must not be \c NULL.
 * \warning The passed in string is duplicated.  It is up to the caller to
 *          manage the memory used by the passed in string.
 * \see slapi_value_init()
 * \see slapi_value_init_berval()
 * \see slapi_value_init_string_passin()
 */
Slapi_Value *slapi_value_init_string(Slapi_Value *v, const char *s);

/* Initialize a \c Slapi_Value with the value contained in a string.
 *
 * \param v The value to initialize.
 * \param s The null-terminated string to be used to initialize the value.
 * \return A pointer to the initialized value.
 * \warning The passed in \c Slapi_Value must not be \c NULL.
 * \warning The passed in string is not duplicated.  Responsibility for the
 *          memory used by the string is handed over to the \c Slapi_Value
 *          structure.
 * \warning The passed in string must not be freed.  It will be freed when
 *          the \c Slapi_Value structure is freed by calling \c slapi_value_free().
 * \see slapi_value_free()
 * \see slapi_value_init()
 * \see slapi_value_init_berval()
 * \see slapi_value_init_string()
 */
Slapi_Value *slapi_value_init_string_passin(Slapi_Value *v, char *s);

/**
 * Duplicate a \c Slapi_Value structure.
 *
 * \param v The value to duplicate.
 * \return A pointer to the copy of the value.
 * \warning If space can not be allocated, the \c ns-slapd program terminates.
 * \warning When you are no longer using the value, free it from memory
 *          by calling slapi_value_free()
 * \warning This function is identical to slapi_value_new_value().
 * \see slapi_value_new_value()
 * \see slapi_value_free()
 */
Slapi_Value *slapi_value_dup(const Slapi_Value *v);

/**
 * Sets the flags in a \c Slapi_Value structure.
 *
 * Valid flags are:
 *     \arg #SLAPI_ATTR_FLAG_NORMALIZED
 *
 * \param v Pointer to the \c Slapi_Value structure for which to
 *        set the flags.
 * \param flags The flags you want to set.
 * \warning The flags support bit-wise operations.
 * \see slapi_values_set_flags()
 * \see slapi_value_get_flags()
 */
void slapi_value_set_flags(Slapi_Value *v, unsigned long flags);

/**
 * Sets the flags in an array of \c Slapi_Value structures.
 *
 * Valid flags are:
 *     \arg #SLAPI_ATTR_FLAG_NORMALIZED
 *
 * \param vs Pointer to the \c Slapi_Value array for which you
 *        want to set the flags.
 * \param flags The flags you want to set.
 * \warning The flags support bit-wise operations.
 * \see slapi_value_set_flags()
 * \see slapi_value_get_flags()
 */
void slapi_values_set_flags(Slapi_Value **vs, unsigned long flags);

/**
 * Retrieves the flags from a \c Slapi_Value structure.
 *
 * \param v Pointer to the \c Slapi_Value structure from which the
 *       flags are to be retrieved.
 * \return The flags that are set in the value.
 * \see slapi_value_set_flags()
 * \see slapi_values_set_flags()
 */
unsigned long slapi_value_get_flags(Slapi_Value *v);

/**
 * Frees a \c Slapi_Value structure from memory.
 *
 * The contents of the value will be freed along with the \c Slapi_Value
 * structure itself.  The pointer will also be set to \c NULL.
 *
 * \param value Address of the pointer to the \c Slapi_Value structure
 *        you wish to free.
 * \see slapi_value_new()
 */
void slapi_value_free(Slapi_Value **value);

/**
 * Gets the \c berval structure of the value.
 *
 * \param value Pointer to the \c Slapi_Value of which you wish
 *        to get the \c berval.
 * \return A pointer to the \c berval structure contained in the
 *         \c Slapi_Value.
 * \warning The returned pointer point to the actual \c berval structure
 *          inside of the value, not a copy.
 * \warning You should not free the returned \c berval structure unless
 *          you plan to replace it by calling \c slapi_value_set_berval().
 * \see slapi_value_set_berval()
 */
const struct berval *slapi_value_get_berval(const Slapi_Value *value);

/**
 * Sets the value of a \c Slapi_Value structure from a \c berval structure.
 *
 * The value is duplicated from the passed in \c berval structure.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to
 *        set the value.
 * \param bval Pointer to the \c berval value to be copied.
 * \return Pointer to the \c Slapi_Value structure passed in as \c value.
 * \return NULL if the passed in value was \c NULL.
 * \warning If the pointer to the \c Slapi_Value structure is \c NULL,
 *          nothing is done, and the function returns \c NULL.
 * \warning If the \c Slapi_Value structure already contains a value, it
 *          is freed from memory before the new one is set.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 * \see slapi_value_get_berval()
 */
Slapi_Value *slapi_value_set_berval(Slapi_Value *value, const struct berval *bval);

/**
 * Sets the value of a \c Slapi_Value structure from another \c Slapi_Value structure.
 *
 * The value is duplicated from the supplied \c Slapi_value structure.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to set
 *        the value.
 * \param vfrom Pointer to the \c Slapi_Value structure from which to
 *        get the value.
 * \return Pointer to the \c Slapi_Value structure passed as the \c value parameter.
 * \return \c NULL if the \c value parameter was \c NULL.
 * \warning The \c vfrom parameter must not be \c NULL.
 * \warning If the pointer to the \c Slapi_Value structure is \c NULL,
 *          nothing is done, and the function returns \c NULL.
 * \warning If the \c Slapi_Value structure already contains a value, it
 *          is freed from memory before the new one is set.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 */
Slapi_Value *slapi_value_set_value(Slapi_Value *value, const Slapi_Value *vfrom);

/**
 * Sets the value of a \c Slapi_Value structure.
 *
 * The value is a duplicate of the data pointed to by \c val and the
 * length \c len.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to set
 *        the value.
 * \param val Pointer to the value.
 * \param len Length of the value.
 * \return Pointer to the \c Slapi_Value structure with the value set.
 * \return \c NULL if the supplied \c Slapi_Value is \c NULL.
 * \warning If the pointer to the \c Slapi_Value structure is \c NULL,
 *          nothing is done, and the function returns \c NULL.
 * \warning If the \c Slapi_Value structure already contains a value, it
 *          is freed from memory before the new one is set.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 */
Slapi_Value *slapi_value_set(Slapi_Value *value, void *val, unsigned long len);

/**
 * Sets the value of a \c Slapi_Value structure from a string.
 *
 * The value is duplicated from a supplied string.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to set
 *        the value.
 * \param strVal The string containing the value to set.
 * \return \c 0 if the value is set.
 * \return \c -1 if the pointer to the \c Slapi_Value is \c NULL.
 * \warning If the pointer to the \c Slapi_Value structure is \c NULL,
 *          nothing is done, and the function returns \c -1.
 * \warning If the \c Slapi_Value structure already contains a value, it
 *          is freed from memory before the new one is set.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 * \see slapi_value_set_string_passin()
 */
int slapi_value_set_string(Slapi_Value *value, const char *strVal);

/**
 * Sets the value of a \c Slapi_Value structure from a string.
 *
 * The supplied string is used as the value within the \c Slapi_Value
 * structure.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to set
 *        the value.
 * \param strVal The string containing the value to set.
 * \return \c 0 if the value is set.
 * \return \c -1 if the pointer to the \c Slapi_Value is \c NULL.
 * \warning Do not free the passed in string pointer to by \c strVal.
 *          Responsibility for the memory used by the string is handed
 *          over to the \c Slapi_Value structure.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 * \see slapi_value_set_string()
 */
int slapi_value_set_string_passin(Slapi_Value *value, char *strVal);

/**
 * Sets the value of a \c Slapi_Value structure from an integer.
 *
 * \param value Pointer to the \c Slapi_Value structure in which to set
 *        the value.
 * \param intVal The integer containing the value to set.
 * \return \c 0 if the value is set.
 * \return \c -1 if the pointer to the \c Slapi_Value is \c NULL.
 * \warning If the pointer to the \c Slapi_Value structure is \c NULL,
 *          nothing is done, and the function returns \c -1.
 * \warning If the \c Slapi_Value structure already contains a value, it
 *          is freed from memory before the new one is set.
 * \warning When you are no longer using the \c Slapi_Value structure, you
 *          should free it from memory by valling \c slapi_value_free().
 * \see slapi_value_free()
 */
int slapi_value_set_int(Slapi_Value *value, int intVal);

/**
 * Retrieves the value of a \c Slapi_Value structure as a string.
 *
 * \param value Pointer to the value you wish to get as a string.
 * \return A string containing the value.
 * \return \c NULL if there is no value.
 * \warning The returned string is the actual value, not a copy.  You
 *          should not free the returned string unless you plan to
 *          replace it by calling slapi_value_set_string().
 * \see slapi_value_set_string()
 */
const char *slapi_value_get_string(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as an integer.
 *
 * \param value Pointer to the value you wish to get as an integer.
 * \return An integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_uint()
 * \see slapi_value_get_long()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_longlong()
 * \see slapi_value_get_ulonglong()
 */
int slapi_value_get_int(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as an unsigned integer.
 *
 * \param value Pointer to the value you wish to get as an unsigned integer.
 * \return An unsigned integer that corresponds to the value stored in
 *         the \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_int()
 * \see slapi_value_get_long()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_longlong()
 * \see slapi_value_get_ulonglong()
 */
unsigned int slapi_value_get_uint(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as a long integer.
 *
 * \param value Pointer to the value you wish to get as a long integer.
 * \return A long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_int()
 * \see slapi_value_get_uint()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_longlong()
 * \see slapi_value_get_ulonglong()
 */
long slapi_value_get_long(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as an unsigned long integer.
 *
 * \param value Pointer to the value you wish to get as an unsigned long integer.
 * \return An unsigned long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_int()
 * \see slapi_value_get_uint()
 * \see slapi_value_get_long()
 * \see slapi_value_get_longlong()
 * \see slapi_value_get_ulonglong()
 */
unsigned long slapi_value_get_ulong(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as a long long integer.
 *
 * \param value Pointer to the value you wish to get as a long long integer.
 * \return A long long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_int()
 * \see slapi_value_get_uint()
 * \see slapi_value_get_long()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_ulonglong()
 */
long long slapi_value_get_longlong(const Slapi_Value *value);

/**
 * Retrieves the value of a \c Slapi_Value structure as an unsigned long long integer.
 *
 * \param value Pointer to the value you wish to get as an unsigned long long integer.
 * \return An unsigned long long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \see slapi_value_get_int()
 * \see slapi_value_get_uint()
 * \see slapi_value_get_long()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_longlong()
 */
unsigned long long slapi_value_get_ulonglong(const Slapi_Value *value);

/**
 * DEPRECATED: Retrieves the value of a \c Slapi_Value structure as a long integer.
 *
 * \param value Pointer to the value you wish to get as a long integer.
 *        The value could end with D or d for days, H or h for hours,
 *        M or m for minutes, S or s for seconds, or no extension.
 * \return A long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \return \c -1 if the given value is invalid.
 * \see slapi_value_get_time_time_t()
 */
long slapi_value_get_timelong(const Slapi_Value *value) __attribute__((deprecated));

/**
 * DEPRECATED: Retrieves the value of a \c Slapi_Value structure as a long long integer.
 *
 * \param value Pointer to the value you wish to get as a long long integer.
 *        The value could end with D or d for days, H or h for hours,
 *        M or m for minutes, S or s for seconds, or no extension.
 * \return A long long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \return \c -1 if the given value is invalid.
 * \see slapi_value_get_time_time_t()
 */
long long slapi_value_get_timelonglong(const Slapi_Value *value) __attribute__((deprecated));

/**
 * Retrieves the value of a \c Slapi_Value structure as a time_t.
 *
 * \param value Pointer to the value you wish to get as a time_t.
 *        The value could end with D or d for days, H or h for hours,
 *        M or m for minutes, S or s for seconds, or no extension.
 * \return A long long integer that corresponds to the value stored in the
 *         \c Slapi_Value structure.
 * \return \c 0 if there is no value.
 * \return \c -1 if the given value is invalid.
 * \see slapi_value_get_int()
 * \see slapi_value_get_uint()
 * \see slapi_value_get_ulong()
 * \see slapi_value_get_longlong()
 * \see slapi_value_get_ulonglong()
 * \see slapi_value_get_time_time_t()
 */
time_t slapi_value_get_time_time_t(const Slapi_Value *value);


/**
 * Gets the length of a value contained in a \c Slapi_Value structure.
 *
 * \param value Pointer to the value of which you wish to get the length.
 * \return The length of the value.
 * \return \c 0 if there is no value.
 */
size_t slapi_value_get_length(const Slapi_Value *value);

/**
 * Compares two \c Slapi_Value structures
 *
 * The matching rule associated with the supplied attribute \c a is used
 * to compare the two values.
 *
 * \param a A pointer to an attribute used to determine how the
 *        two values will be compared.
 * \param v1 Pointer to the \c Slapi_Value structure containing the first
 *        value to compare.
 * \param v2 Pointer to the \c Slapi_Value structure containing the second
 *        value to compare.
 * \return \c 0 if the two values are equal.
 * \return \c -1 if \c v1 is smaller than \c v2.
 * \return \c 1 if \c v1 is greater than \c v2.
 */
int slapi_value_compare(const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2);

/**
 * Free and initialize the contents of the berval without freeing the
 * struct berval * itself.  This is useful when you use struct berval
 * which are allocated on the stack e.g.
 * \code
 * struct berval bv = {0, NULL};
 * slapi_ber_bvcpy(&bv, someotherbvp);
 * // use bv
 * slapi_ber_bvdone(&bv);
 * \endcode
 * You must only call slapi_ber_bvdone() if the bv_val member has been
 * allocated with malloc() or is NULL.
 *
 * \param bvp A struct berval * you want to free the contents of
 * \return \c void
 */
void slapi_ber_bvdone(struct berval *bvp);

/**
 * Copy the contents of the berval without allocating a new struct berval*
 * This is useful when you use struct berval which are allocated on the stack e.g.
 * \code
 * struct berval bv = {0, NULL};
 * slapi_ber_bvcpy(&bv, someotherbvp);
 * // use bv
 * slapi_ber_bvdone(&bv);
 * \endcode
 * The bvs source berval to copy from can either be allocated on the heap
 * or the stack.
 *
 * \param bvd A struct berval * you want copy to
 * \param bvs A struct berval * you want to copy from
 * \return \c void
 */
void slapi_ber_bvcpy(struct berval *bvd, const struct berval *bvs);

/*
 * Valueset functions.
 */

/**
 * Flag that indicates that the value should be used by reference.
 *
 * \see slapi_valueset_add_value_ext()
 */
#define SLAPI_VALUE_FLAG_PASSIN          0x1
#define SLAPI_VALUE_FLAG_IGNOREERROR     0x2
#define SLAPI_VALUE_FLAG_PRESERVECSNSET  0x4
#define SLAPI_VALUE_FLAG_USENEWVALUE     0x8  /* see valueset_remove_valuearray */
#define SLAPI_VALUE_FLAG_DUPCHECK       0x10  /* used in valueset_add... */

/**
 * Creates an empty \c Slapi_ValueSet structure.
 *
 * \return Pointer to the newly allocated \c Slapi_ValueSet structure.
 * \warning If no space can be allocated (for example, if no more virtual
 *          memory exists), the \c ns-slapd program terminates.
 * \warning When you are no longer using the valueset, you should free it
 *          from memory by calling \c slapi_valueset_free().
 * \see slapi_valueset_free()
 */
Slapi_ValueSet *slapi_valueset_new(void);

/**
 * Free a \c Slapi_ValueSet structure from memory.
 *
 * Call this function when you are done working with the structure.
 * All members of the valueset will be freed as well if they are not
 * \c NULL.
 *
 * \param vs Pointer to the \c Slapi_ValueSet to free.
 * \see slapi_valueset_done()
 */
void slapi_valueset_free(Slapi_ValueSet *vs);

/**
 * Initializes a \c Slapi_ValueSet structure.
 *
 * All values inside of the structure will be cleared (set to \c 0).
 * The values will not be freed by this function.  To free the values
 * first, call \c slapi_valueset_done().
 *
 * \param vs Pointer to the \c Slapi_ValueSet to initialize.
 * \warning When you are no longer using the \c Slapi_ValueSet structure,
 *          you should free it from memory by calling \c slapi_valueset_free().
 * \see slapi_valueset_done()
 * \see slapi_valueset_free()
 */
void slapi_valueset_init(Slapi_ValueSet *vs);

/**
 * Frees the values contained in a \c Slapi_ValueSet structure.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure from which
 *        you want to free its values.
 * \warning Use this function when you are no longer using the values
 *          but you want to re-use the \c Slapi_ValueSet structure for
 *          a new set of values.
 * \see slapi_valueset_init()
 */
void slapi_valueset_done(Slapi_ValueSet *vs);

/**
 * Adds a value to a \c Slapi_ValueSet structure.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure to which to
 *        add the value.
 * \param addval Pointer to the \c Slapi_Value structure to add to
 *        the \c Slapi_ValueSet.
 * \warning The value is duplicated from the \c Slapi_Value structure,
 *          which can be freed frmo memory without altering the
 *          \c Slapi_ValueSet structure.
 * \warning This function does not verify if the value is already present
 *          in the \c Slapi_ValueSet structure.  You can manually check
 *          this using \c slapi_valueset_first_value() and
 *          \c slapi_valueset_next_value().
 * \see slapi_valueset_first_value()
 * \see slapi_valueset_next_value()
 */
void slapi_valueset_add_value(Slapi_ValueSet *vs, const Slapi_Value *addval);

/**
 * Adds a value to a \c Slapi_ValueSet structure with optional flags.
 *
 * This function is similar to \c slapi_valueset_add_value(), but it
 * allows optional flags to be specified to allow the new value to be
 * used by reference.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure to which to
 *        add the value.
 * \param addval Pointer to the \c Slapi_Value structure to add to
 *        the \c Slapi_ValueSet.
 * \param flags If #SLAPI_VALUE_FLAG_PASSIN bit is set in the flags,
 *        the function will take over the ownership of the new value
 *        to be added without duplicating it.
 * \warning This function does not verify if the value is already present
 *          in the \c Slapi_ValueSet structure.  You can manually check
 *          this using \c slapi_valueset_first_value() and
 *          \c slapi_valueset_next_value().
 * \see slapi_valueset_add_value()
 * \see slapi_valueset_first_value()
 * \see slapi_valueset_next_value()
 */
void slapi_valueset_add_value_ext(Slapi_ValueSet *vs, const Slapi_Value *addval, unsigned long flags);
int slapi_valueset_add_attr_value_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *addval, unsigned long flags);

/**
 * Gets the first value in a \c Slapi_ValueSet structure.
 *
 * This function can be used with \c slapi_valueset_next_value() to
 * iterate through all values in a \c Slapi_ValueSet structure.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure from which
 *        you wish to get the value.
 * \param v Address of the pointer to the \c Slapi_Value structure
 *        for the returned value.
 * \return The index of the value in the Slapi_ValueSet structure.
 * \return \c -1 if there was no value.
 * \warning This function gives a pointer to the actual value within
 *          the \c Slapi_ValueSet structure.  You should not free it
 *          from memory.
 * \warning You will need to pass this index to slapi_valueset_next_value()
 *          if you wish to iterate through all values in the valueset.
 * \see slapi_valueset_next_value().
 */
int slapi_valueset_first_value(Slapi_ValueSet *vs, Slapi_Value **v);

/**
 * Gets the next value in a \c Slapi_ValueSet structure.
 *
 * This is part of a pair of iterator functions.  It should be
 * called after first calling \c slapi_valueset_first_value().
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure from which
 *        you wish to get the value.
 * \param index Value returned by the previous call to \c slapi_valueset_first_value()
 *        or \c slapi_valueset_next_value().
 * \param v Address of the pointer to the \c Slapi_Value structure
 *        for the returned value.
 * \return The index of the value in the Slapi_ValueSet structure.
 * \return \c -1 if there was no value.
 * \warning This function gives a pointer to the actual value within
 *          the \c Slapi_ValueSet structure.  You should not free it
 *          from memory.
 * \warning You will need to pass this index to slapi_valueset_next_value()
 *          if you wish to iterate through all values in the valueset.
 * \see slapi_valueset_first_value()
 */
int slapi_valueset_next_value(Slapi_ValueSet *vs, int index, Slapi_Value **v);

/**
 * Returns the number of values contained in a \c Slapi_ValueSet structure.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure of which
 *        you wish to get the count.
 * \return The number of values contained in the \c Slapi_ValueSet structure.
 */
int slapi_valueset_count(const Slapi_ValueSet *vs);

/**
 * Checks if  a \c Slapi_ValueSet structure has values
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure of which
 *        you wish to get the count.
 * \return 1 if there are no values contained in the \c Slapi_ValueSet structure.
 * \return 0 if there are values contained in the \c Slapi_ValueSet structure.
 */
int slapi_valueset_isempty(const Slapi_ValueSet *vs);

/**
 * Initializes a \c Slapi_ValueSet with copies of the values of a \c Slapi_Mod structure.
 *
 * \param vs Pointer to the \c Slapi_ValueSet structure into which
 *        you wish to copy the values.
 * \param smod Pointer to the \c Slapi_Mod structure from which you
 *        want to copy the values.
 * \warning This function does not verify that the \c Slapi_ValueSet
 *          structure already contains values, so it is your responsibility
 *          to verify that there are no values prior to calling this function.
 *          If you do not verify this, the allocated memory space will leak.
 *          You can free existing values by calling slapi_valueset_done().
 * \see slapi_valueset_done()
 */
void slapi_valueset_set_from_smod(Slapi_ValueSet *vs, Slapi_Mod *smod);

/**
 * Initializes a \c Slapi_ValueSet with copies of the values of another \c Slapi_ValueSet.
 *
 * \param vs1 Pointer to the \c Slapi_ValueSet structure into which
 *        you wish to copy the values.
 * \param vs2 Pointer to the \c Slapi_ValueSet structure from which
 *        you want to copy the values.
 * \warning This function does not verify that the \c Slapi_ValueSet
 *          structure already contains values, so it is your responsibility
 *          to verify that there are no values prior to calling this function.
 *          If you do not verify this, the allocated memory space will leak.
 *          You can free existing values by calling slapi_valueset_done().
 * \see slapi_valueset_done()
 */
void slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2);
void slapi_valueset_join_attr_valueset(const Slapi_Attr *a, Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2);

/**
 * Finds a requested value in a valueset.
 *
 * The syntax of a supplied attribute will be used to compare the values.
 * This function can be used to check for duplicate values in a valueset.
 *
 * \param a Pointer to the attribute. This is used to determine the
 *        syntax of the values and how to match them.
 * \param vs Pointer to the \c Slapi_ValueSet structure from which
 *        you wish to find the value.
 * \param v Pointer to the \c Slapi_Value structure containing the
 *        value that you wish to find.
 * \return Pointer to the value in the valueset if the value was found.
 * \return \c NULL if the value was not found.
 * \warning The returned pointer points to the actual value in the
 *          \c Slapi_ValueSet structure.  It should not be freed.
 */
Slapi_Value *slapi_valueset_find(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v);


/* TODO - Pickup Doxygen work here */
/*
 * operation routines
 */
int slapi_op_abandoned(Slapi_PBlock *pb);
int slapi_is_operation_abandoned(Slapi_Operation *op);
unsigned long slapi_op_get_type(Slapi_Operation *op);
void slapi_operation_set_flag(Slapi_Operation *op, unsigned long flag);
void slapi_operation_clear_flag(Slapi_Operation *op, unsigned long flag);
int slapi_operation_is_flag_set(Slapi_Operation *op, unsigned long flag);
ber_tag_t slapi_operation_get_tag(Slapi_Operation *op);
ber_int_t slapi_operation_get_msgid(Slapi_Operation *op);
void slapi_operation_set_tag(Slapi_Operation *op, ber_tag_t tag);
void slapi_operation_set_msgid(Slapi_Operation *op, ber_int_t msgid);
int slapi_op_reserved(Slapi_PBlock *pb);
void slapi_operation_set_csngen_handler(Slapi_Operation *op, void *callback);
void slapi_operation_set_replica_attr_handler(Slapi_Operation *op, void *callback);
int slapi_operation_get_replica_attr(Slapi_PBlock *pb, Slapi_Operation *op, const char *type, void *value);
char *slapi_op_type_to_string(unsigned long type);
int slapi_op_internal(Slapi_PBlock *pb);
Slapi_Operation *slapi_operation_new(int flags);

/*
 * connection routines
 */

int slapi_connection_acquire(Slapi_Connection *conn);
int slapi_connection_remove_operation(Slapi_PBlock *pb, Slapi_Connection *conn, Slapi_Operation *op, int release);

/*
 * LDAPMod manipulation routines
 */
Slapi_Mods *slapi_mods_new(void);
void slapi_mods_init(Slapi_Mods *smods, int initCount);
void slapi_mods_init_byref(Slapi_Mods *smods, LDAPMod **mods);
void slapi_mods_init_passin(Slapi_Mods *smods, LDAPMod **mods);
void slapi_mods_free(Slapi_Mods **smods);
void slapi_mods_done(Slapi_Mods *smods);
void slapi_mods_insert_at(Slapi_Mods *smods, LDAPMod *mod, int pos);
void slapi_mods_insert_smod_at(Slapi_Mods *smods, Slapi_Mod *smod, int pos);
void slapi_mods_insert_before(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_insert_smod_before(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_insert_after(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_insert_after(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_add(Slapi_Mods *smods, int modtype, const char *type, unsigned long len, const char *val);
void slapi_mods_add_ldapmod(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_add_modbvps(Slapi_Mods *smods, int modtype, const char *type, struct berval **bvps);
void slapi_mods_add_mod_values(Slapi_Mods *smods, int modtype, const char *type, Slapi_Value **va);
void slapi_mods_add_smod(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_add_string(Slapi_Mods *smods, int modtype, const char *type, const char *val);
void slapi_mods_remove(Slapi_Mods *smods);
LDAPMod *slapi_mods_get_first_mod(Slapi_Mods *smods);
LDAPMod *slapi_mods_get_next_mod(Slapi_Mods *smods);
Slapi_Mod *slapi_mods_get_first_smod(Slapi_Mods *smods, Slapi_Mod *smod);
Slapi_Mod *slapi_mods_get_next_smod(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_iterator_backone(Slapi_Mods *smods);
LDAPMod **slapi_mods_get_ldapmods_byref(Slapi_Mods *smods);
LDAPMod **slapi_mods_get_ldapmods_passout(Slapi_Mods *smods);
int slapi_mods_get_num_mods(const Slapi_Mods *smods);
void slapi_mods_dump(const Slapi_Mods *smods, const char *text);

Slapi_Mod *slapi_mod_new(void);
void slapi_mod_init(Slapi_Mod *smod, int initCount);
void slapi_mod_init_byval(Slapi_Mod *smod, const LDAPMod *mod);
void slapi_mod_init_byref(Slapi_Mod *smod, LDAPMod *mod);
void slapi_mod_init_passin(Slapi_Mod *smod, LDAPMod *mod);
/* init a mod and set the mod values to be a copy of the given valueset */
void slapi_mod_init_valueset_byval(Slapi_Mod *smod, int op, const char *type, const Slapi_ValueSet *svs);
void slapi_mod_add_value(Slapi_Mod *smod, const struct berval *val);
void slapi_mod_remove_value(Slapi_Mod *smod);
struct berval *slapi_mod_get_first_value(Slapi_Mod *smod);
struct berval *slapi_mod_get_next_value(Slapi_Mod *smod);
const char *slapi_mod_get_type(const Slapi_Mod *smod);
int slapi_mod_get_operation(const Slapi_Mod *smod);
void slapi_mod_set_type(Slapi_Mod *smod, const char *type);
void slapi_mod_set_operation(Slapi_Mod *smod, int op);
int slapi_mod_get_num_values(const Slapi_Mod *smod);
const LDAPMod *slapi_mod_get_ldapmod_byref(const Slapi_Mod *smod);
LDAPMod *slapi_mod_get_ldapmod_passout(Slapi_Mod *smod);
void slapi_mod_free(Slapi_Mod **smod);
void slapi_mod_done(Slapi_Mod *mod);
int slapi_mod_isvalid(const Slapi_Mod *mod);
void slapi_mod_dump(LDAPMod *mod, int n);


/* helper functions to translate between entry and a set of mods */
int slapi_mods2entry(Slapi_Entry **e, const char *dn, LDAPMod **attrs);
int slapi_entry2mods(const Slapi_Entry *e, char **dn, LDAPMod ***attrs);

#if defined(USE_OLD_UNHASHED)
int is_type_forbidden(const char *type);
#endif

/*
 * routines for dealing with filters
 */
int slapi_filter_get_choice(Slapi_Filter *f);
int slapi_filter_get_ava(Slapi_Filter *f, char **type, struct berval **bval);
int slapi_filter_get_attribute_type(Slapi_Filter *f, char **type);
int slapi_filter_get_subfilt(Slapi_Filter *f, char **type, char **initial, char ***any, char ** final);
Slapi_Filter *slapi_filter_list_first(Slapi_Filter *f);
Slapi_Filter *slapi_filter_list_next(Slapi_Filter *f, Slapi_Filter *fprev);
Slapi_Filter *slapi_str2filter(char *str);
Slapi_Filter *slapi_filter_join(int ftype, Slapi_Filter *f1, Slapi_Filter *f2);
Slapi_Filter *slapi_filter_join_ex(int ftype, Slapi_Filter *f1, Slapi_Filter *f2, int recurse_always);

void slapi_filter_free(Slapi_Filter *f, int recurse);
int slapi_filter_test(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f, int verify_access);
int slapi_vattr_filter_test(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *f, int verify_access);
int slapi_filter_test_simple(Slapi_Entry *e, Slapi_Filter *f);
char *slapi_find_matching_paren(const char *str);
int slapi_filter_test_ext(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f, int verify_access, int only_test_access);
int slapi_vattr_filter_test_ext(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f, int verify_access, int only_test_access);
int slapi_filter_compare(struct slapi_filter *f1, struct slapi_filter *f2);
Slapi_Filter *slapi_filter_dup(Slapi_Filter *f);
int slapi_filter_changetype(Slapi_Filter *f, const char *newtype);
char *slapi_filter_escape_filter_value(char* filter_attr, char *filter_value);

int slapi_attr_is_last_mod(char *attr);

/**
 * Normalize in-place the given filter.  Normalizes the attribute types always.
 * If norm_values is true, will also normalize the values.
 *
 * \param f the filter to normalize
 * \param norm_values if true, normalize the values in addition to the type names
 */
void slapi_filter_normalize(Slapi_Filter *f, PRBool norm_values);

/**
 * Check whether a given attribute type is defined in schema or not
 *
 * \param attribute type name to be checked
 * \return \c 0 if the attribute type is not defined in schema
 * \return non-0 if the attribute type is defined in schema
 */
int slapi_attr_syntax_exists(const char *type);

/**
 * Reload internally registered attribute syntaxes.
 *
 * \param none
 * \return \c 0 if the reload was successful.
 * \return non-0 if the reload failed.
 */
int slapi_reload_internal_attr_syntax(void);

/*
 * slapi_filter_apply() is used to apply a function to each simple filter
 * component within a complex filter.  A 'simple filter' is anything other
 * than AND, OR or NOT.
 */
typedef int (*FILTER_APPLY_FN)(Slapi_Filter *f, void *arg);
int slapi_filter_apply(struct slapi_filter *f, FILTER_APPLY_FN fn, void *arg, int *error_code);
/*
 * Possible return values for slapi_filter_apply() and FILTER_APPLY_FNs.
 * Note that a FILTER_APPLY_FN should return _STOP or _CONTINUE only.
 */
#define SLAPI_FILTER_SCAN_STOP     -1  /* premature abort */
#define SLAPI_FILTER_SCAN_ERROR    -2  /* an error occurred */
#define SLAPI_FILTER_SCAN_NOMORE   0   /* success */
#define SLAPI_FILTER_SCAN_CONTINUE 1   /* continue scanning */
/* Error codes that slapi_filter_apply() may set in *error_code */
#define SLAPI_FILTER_UNKNOWN_FILTER_TYPE 2


/*
 * Bit-Twiddlers
 */
unsigned char slapi_setbit_uchar(unsigned char f, unsigned char bitnum);
unsigned char slapi_unsetbit_uchar(unsigned char f, unsigned char bitnum);
int slapi_isbitset_uchar(unsigned char f, unsigned char bitnum);
unsigned int slapi_setbit_int(unsigned int f, unsigned int bitnum);
unsigned int slapi_unsetbit_int(unsigned int f, unsigned int bitnum);
int slapi_isbitset_int(unsigned int f, unsigned int bitnum);


/*
 * routines for sending entries and results to the client
 */
int slapi_send_ldap_search_entry(Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **ectrls, char **attrs, int attrsonly);
void slapi_send_ldap_result(Slapi_PBlock *pb, int err, char *matched, char *text, int nentries, struct berval **urls);
int slapi_send_ldap_referral(Slapi_PBlock *pb, Slapi_Entry *e, struct berval **refs, struct berval ***urls);
int slapi_send_ldap_intermediate(Slapi_PBlock *pb, LDAPControl **ectrls, char *responseName, struct berval *responseValue);
typedef int (*send_ldap_search_entry_fn_ptr_t)(Slapi_PBlock *pb,
                                               Slapi_Entry *e,
                                               LDAPControl **ectrls,
                                               char **attrs,
                                               int attrsonly);
typedef void (*send_ldap_result_fn_ptr_t)(Slapi_PBlock *pb, int err, char *matched, char *text, int nentries, struct berval **urls);
typedef int (*send_ldap_referral_fn_ptr_t)(Slapi_PBlock *pb,
                                           Slapi_Entry *e,
                                           struct berval **refs,
                                           struct berval ***urls);


/*
 * matching rule
 */
typedef int (*mrFilterMatchFn)(void *filter, Slapi_Entry *, Slapi_Attr *vals);
/* returns:  0  filter matched
 *        -1  filter did not match
 *        >0  an LDAP error code
 */
int slapi_mr_indexer_create(Slapi_PBlock *opb);
int slapi_mr_filter_index(Slapi_Filter *f, Slapi_PBlock *pb);
int slapi_berval_cmp(const struct berval *L, const struct berval *R);
#define SLAPI_BERVAL_EQ(L, R) ((L)->bv_len == (R)->bv_len && \
                               !memcmp((L)->bv_val, (R)->bv_val, (L)->bv_len))

typedef struct slapi_matchingRuleEntry
{
    char *mr_oid;            /* the official oid */
    char *mr_oidalias;       /* not currently used */
    char *mr_name;           /* the official name */
    char *mr_desc;           /* a description */
    char *mr_syntax;         /* the assertion syntax OID */
    int mr_obsolete;         /* is mr obsolete? */
    char **mr_compat_syntax; /* list of OIDs of other syntaxes that can use this matching rule */
} slapi_matchingRuleEntry;
typedef struct slapi_matchingRuleEntry Slapi_MatchingRuleEntry;

Slapi_MatchingRuleEntry *slapi_matchingrule_new(void);
void slapi_matchingrule_free(Slapi_MatchingRuleEntry **mrEntry,
                             int freeMembers);
int slapi_matchingrule_get(Slapi_MatchingRuleEntry *mr, int arg, void *value);
int slapi_matchingrule_set(Slapi_MatchingRuleEntry *mr, int arg, void *value);
int slapi_matchingrule_register(Slapi_MatchingRuleEntry *mrEntry);
int slapi_matchingrule_unregister(char *oid);

/**
 * Is the given matching rule an ordering matching rule and is it
 * compatible with the given syntax?
 *
 * \param name_or_oid Name or OID of a matching rule
 * \param syntax_oid OID of a syntax
 * \return \c TRUE if the matching rule is an ordering rule and can be used by the given syntax
 * \return \c FALSE otherwise
 */
int slapi_matchingrule_is_ordering(const char *oid_or_name, const char *syntax_oid);

/**
 * Can the given syntax OID use the given matching rule name/OID? A
 * matching rule can apply to more than one syntax.  Use this function
 * to determine if the given syntax can use the given matching rule.
 *
 * \param mr_name_or_oid Name or OID of a matching rule
 * \param syntax_oid OID of a syntax
 * \return \c TRUE if the syntax can be used with the matching rule
 * \return \c FALSE otherwise
 */
int slapi_matchingrule_is_compat(const char *mr_oid_or_name, const char *syntax_oid);

/**
 * In certain cases, we can just use a simple compare function to
 * generate index keys.  The compare function is usually provided
 * by the syntax plugin.  If this is the case, we can skip generating
 * an indexer in the index config code.
 *
 * \param mr_name_or_oid Name or OID of a matching rule
 * \return \c TRUE if the matching rule can use a simple compare function
 * \return \c FALSE otherwise
 */
int slapi_matchingrule_can_use_compare_fn(const char *mr_oid_or_name);

/*
 * access control
 */
int slapi_access_allowed(Slapi_PBlock *pb, Slapi_Entry *e, char *attr, struct berval *val, int access);
int slapi_acl_check_mods(Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, char **errbuf);
int slapi_acl_verify_aci_syntax(Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf);


/*
 * attribute stuff
 */
int slapi_value_find(void *plugin, struct berval **vals, struct berval *v);


/*
 * password handling
 */
#define SLAPI_USERPWD_ATTR "userpassword"
int slapi_pw_find_sv(Slapi_Value **vals, const Slapi_Value *v);
/* value encoding encoding */
/* checks if the value is encoded with any known algorithm*/
int slapi_is_encoded(char *value);
/* encode value with the specified algorithm */
char *slapi_encode(char *value, char *alg);
/* encode value with the specified algorithm, or with local algorithm if pb
 * and sdn are specified instead, or global algorithm if pb and sdn are null */
char *slapi_encode_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, char *value, char *alg);


/* UTF8 related */
int slapi_has8thBit(unsigned char *s);
unsigned char *slapi_utf8StrToLower(unsigned char *s);
void slapi_utf8ToLower(unsigned char *s, unsigned char *d, int *ssz, int *dsz);
int slapi_utf8isUpper(unsigned char *s);
unsigned char *slapi_utf8StrToUpper(unsigned char *s);
void slapi_utf8ToUpper(unsigned char *s, unsigned char *d, int *ssz, int *dsz);
int slapi_utf8isLower(unsigned char *s);
int slapi_utf8casecmp(unsigned char *s0, unsigned char *s1);
int slapi_utf8ncasecmp(unsigned char *s0, unsigned char *s1, int n);

unsigned char *slapi_UTF8STRTOLOWER(char *s);
void slapi_UTF8TOLOWER(char *s, char *d, int *ssz, int *dsz);
int slapi_UTF8ISUPPER(char *s);
unsigned char *slapi_UTF8STRTOUPPER(char *s);
void slapi_UTF8TOUPPER(char *s, char *d, int *ssz, int *dsz);
int slapi_UTF8ISLOWER(char *s);
int slapi_UTF8CASECMP(char *s0, char *s1);
int slapi_UTF8NCASECMP(char *s0, char *s1, int n);


/*
 * Interface to the API broker service
 *
 * The API broker allows plugins to publish an API that may be discovered
 * and used dynamically at run time by other subsystems e.g. other plugins.
 */

/* Function:    slapi_apib_register
   Description:    this function allows publication of an interface
   Parameters:    guid - a string constant that uniquely identifies the
            interface (must exist for the life of the server)
        api - a vtable for the published api (must exist for the
            life of the server or until the reference count,
            if it exists, reaches zero)
   Return:    0 if function succeeds
        non-zero otherwise
*/
int slapi_apib_register(char *guid, void **api); /* publish an interface */

/* Function:    slapi_apib_unregister
   Description:    this function allows removal of a published interface
   Parameters:    guid - a string constant that uniquely identifies the interface
   Return:    0 if function succeeds
        non-zero otherwise
*/
int slapi_apib_unregister(char *guid); /* remove interface from published list */


/* Function:    slapi_apib_get_interface
   Description:    this function allows retrieval of a published interface,
            if the api reference counted, then the reference
            count is incremented
   Parameters:    guid - a string constant that uniquely identifies the
            interface requested
        api - the retrieved vtable for the published api (must NOT
            be freed)
   Return:    0 if function succeeds
        non-zero otherwise
*/
int slapi_apib_get_interface(char *guid, void ***api); /* retrieve an interface for use */

/* Function:    slapi_apib_get_interface_all
   Description:    this function allows retrieval of a published interface,
            if the api reference counted, then the reference
            count is incremented
            This will return all published interfaces for a given guid
   Parameters:    guid - a string constant that uniquely identifies the
            interface requested
        apilist - the retrieved list of vtables for the published api - caller
            must free the apilist, but not the apilist elements
   Return:    0 if function succeeds
        non-zero otherwise
*/
int slapi_apib_get_interface_all(char *guid, void ****apilist); /* retrieve all interfaces for guid */


/* Function:    slapi_apib_make_reference_counted
   Description:    this function makes an interface a reference counted interface
            it must be called prior to registering the interface
   Parameters:    api - the api to make a reference counted api
        callback - if non-zero, this must be a pointer to a function
            which the api broker will call when the ref count for
            the api reaches zero.  This function must return 0 if
            it deregisters the api, non-zero otherwise
        api - the retrieved vtable for the published api (must NOT
            be freed)
   Return:    0 if function succeeds
        non-zero otherwise
*/
typedef int (*slapi_apib_callback_on_zero)(void **api);

int slapi_apib_make_reference_counted(void **api,
                                      slapi_apib_callback_on_zero callback);


/* Function:    slapi_apib_addref
   Description:    this function adds to the reference count of an api - a
            call to this function should be paired with a call
            to slapi_apib_release
         - ONLY FOR REFERENCE COUNTED APIS
   Parameters:    api - the api to add a reference to
   Return:    the new reference count
*/
int slapi_apib_addref(void **api);


/* Function:    slapi_apib_release
   Description:    this function adds to the reference count of an api - a
            call to this function should be paired with a prior call
            to slapi_apib_addref or slapi_apib_get_interface
        - ONLY FOR REFERENCE COUNTED APIS
   Parameters:    api - the api to add a reference to
   Return:    the new reference count
*/
int slapi_apib_release(void **api);

/**** End of API broker interface. *******************************************/

/*
 * routines for dealing with controls
 */
int slapi_control_present(LDAPControl **controls, char *oid, struct berval **val, int *iscritical);
void slapi_register_supported_control(char *controloid,
                                      unsigned long controlops);
LDAPControl *slapi_dup_control(LDAPControl *ctrl);

#define SLAPI_OPERATION_BIND     0x00000001UL
#define SLAPI_OPERATION_UNBIND   0x00000002UL
#define SLAPI_OPERATION_SEARCH   0x00000004UL
#define SLAPI_OPERATION_MODIFY   0x00000008UL
#define SLAPI_OPERATION_ADD      0x00000010UL
#define SLAPI_OPERATION_DELETE   0x00000020UL
#define SLAPI_OPERATION_MODDN    0x00000040UL
#define SLAPI_OPERATION_MODRDN   SLAPI_OPERATION_MODDN
#define SLAPI_OPERATION_COMPARE  0x00000080UL
#define SLAPI_OPERATION_ABANDON  0x00000100UL
#define SLAPI_OPERATION_EXTENDED 0x00000200UL
#define SLAPI_OPERATION_ANY      0xFFFFFFFFUL
#define SLAPI_OPERATION_NONE     0x00000000UL
int slapi_get_supported_controls_copy(char ***ctrloidsp,
                                      unsigned long **ctrlopsp);
int slapi_build_control(char *oid, BerElement *ber, char iscritical, LDAPControl **ctrlp);
int slapi_build_control_from_berval(char *oid, struct berval *bvp, char iscritical, LDAPControl **ctrlp);

/* Given an array of controls e.g. LDAPControl **ctrls, add the given
   control to the end of the array, growing the array with realloc
   e.g. slapi_add_control_ext(&ctrls, newctrl, 1);
   if ctrls is NULL, the array will be created with malloc
   if copy is true, the given control will be copied
   if copy is false, the given control will be used and owned by the array
   if copy is false, make sure the control can be freed by ldap_controls_free
*/
void slapi_add_control_ext(LDAPControl ***ctrlsp, LDAPControl *newctrl, int copy);

/* Given an array of controls e.g. LDAPControl **ctrls, add all of the given
   controls in the newctrls array to the end of ctrls, growing the array with realloc
   if ctrls is NULL, the array will be created with malloc
   if copy is true, each given control will be copied
   if copy is false, each given control will be used and owned by the array
   if copy is false, make sure each control can be freed by ldap_controls_free
*/
void slapi_add_controls(LDAPControl ***ctrlsp, LDAPControl **newctrls, int copy);

/*
 * routines for dealing with extended operations
 */
char **slapi_get_supported_extended_ops_copy(void);

/*
 * routines for dealing with supported features
 */

#ifndef LDAP_FEATURE_ALL_OP_ATTRS
#define LDAP_FEATURE_ALL_OP_ATTRS "1.3.6.1.4.1.4203.1.5.1"
#endif

int slapi_get_supported_features_copy(char ***ftroidsp);

/*
 * bind, including SASL
 */
void slapi_register_supported_saslmechanism(char *mechanism);
char **slapi_get_supported_saslmechanisms_copy(void);
void slapi_add_auth_response_control(Slapi_PBlock *pb, const char *binddn);
int slapi_add_pwd_control(Slapi_PBlock *pb, char *arg, long time);
int slapi_pwpolicy_make_response_control(Slapi_PBlock *pb, int seconds, int logins, int error);
/* Password Policy Response Control stuff - the error argument above */
#define LDAP_PWPOLICY_PWDEXPIRED       0
#define LDAP_PWPOLICY_ACCTLOCKED       1
#define LDAP_PWPOLICY_CHGAFTERRESET    2
#define LDAP_PWPOLICY_PWDMODNOTALLOWED 3
#define LDAP_PWPOLICY_MUSTSUPPLYOLDPWD 4
#define LDAP_PWPOLICY_INVALIDPWDSYNTAX 5
#define LDAP_PWPOLICY_PWDTOOSHORT      6
#define LDAP_PWPOLICY_PWDTOOYOUNG      7
#define LDAP_PWPOLICY_PWDINHISTORY     8

/*
 * Password Policy API
 */
/**
 * Gets the password policy object for a given entry
 *
 * \param dn The dn of the entry whose password policy object you
 *        want returned
 *
 * \return A pointer to an opaque password policy object
 * \return \c NULL if there is a problem getting the policy object
 * \warning The caller should free the returned password policy
 *          object when finished by calling the slapi_pwpolicy_free()
 *          function.
 * \see slapi_pwpolicy_free()
 */
Slapi_PWPolicy *slapi_get_pwpolicy(Slapi_DN *dn);

/**
 * Free a password policy object from memory
 *
 * \param pwpolicy The password policy object that you want to free
 * \see slapi_get_pwpolicy()
 */
void slapi_pwpolicy_free(Slapi_PWPolicy *pwpolicy);

/**
 * Checks a password policy object to see if the password is expired
 *
 * \param pwpolicy The password policy object that you want to check
 * \param e The entry that you want to check
 * \param expire_time Fills in the expiration time if the password has not expired yet.
 *        You can pass \c NULL if you are not interested in the expiration time.
 * \param remaining_grace Fills in the number of grace logins remaining if the password
 *        has already expired.  You can pass \c NULL if you are not interested in the
 *        number of remaining grace logins.
 *
 * \return \c 1 if the password has expired
 * \return \c 0 if the password has not expired
 */
int slapi_pwpolicy_is_expired(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *expire_time, int *remaining_grace);

/**
 * Checks a password policy to see if an account is locked
 *
 * \param pwpolicy The password policy object that you want to check
 * \param e The entry that you want to check
 * \param unlock_time Fills in the time the account will be unlocked if the account
 *        is currently locked.  You can pass \c NULL if you are not interested in the
 *        unlock time.
 *
 * \return \c 1 if the account is locked
 * \return \c 0 if the account is not locked
 */
int slapi_pwpolicy_is_locked(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *unlock_time);

/**
 * Checks a password policy to see if a password has been reset
 *
 * \param pwpolicy The password policy object that you want to check
 * \param e The entry that you want to check
 *
 * \return \c 1 if the password has been reset
 * \return \c 0 if the password has not been reset
 */
int slapi_pwpolicy_is_reset(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e);

/**
 * Free an array of strings from memory
 *
 * \param array The array that you want to free
 * \see slapi_ch_array_add()
 * \see slapi_ch_array_dup()
 */
void slapi_ch_array_free(char **array);

/**
 * Duplicate an array of strings
 *
 * \param array The array that you want to duplicate
 * \return A newly allocated copy of \c array
 * \return \c NULL if there is a problem duplicating the array
 * \warning The caller should free the returned array when finished
 *          by calling the slapi_ch_array_free() function.
 * \see slapi_ch_array_free()
 */
char **slapi_ch_array_dup(char **array);

/**
 * Add a string to an array of strings
 *
 * \param array The array to add the string to
 * \param string The string to add to the array
 * \warning The \c string parameter is not copied.  If you do not
 *          want to hand the memory used by \c string over to the
 *          array, you should duplicate it first by calling the
 *          slapi_ch_strdup() function.
 * \warning If \c *a is \c NULL, a new array will be allocated.
 * \see slapi_ch_array_free()
 */
void slapi_ch_array_add(char ***array, char *string);

/**
 * Add a string to an array of strings
 *
 * \param array The array to add the string to
 * \param string The string to add to the array
 * \warning The \c string parameter is not copied.  If you do not
 *          want to hand the memory used by \c string over to the
 *          array, you should duplicate it first by calling the
 *          slapi_ch_strdup() function.
 * \warning If \c *a is \c NULL, a new array will be allocated.
 * \see slapi_ch_array_free()
 * \return the total number of elements in the array.
 */
int slapi_ch_array_add_ext(char ***array, char *string);

/**
 * Find a string in an array of strings
 *
 * \param array The array of strings
 * \param string The string to look for in the array
 * \return NULL if there is no match
 * \return 1 if there is a match
 */
int slapi_ch_array_utf8_inlist(char **array, char *string);

/**
 * Returns the error string of an ldap result code, but it can also handle
 * library errors(negative result codes)
 *
 * \param result The result code
 * \return The error text string of the result code
 */
char *slapi_err2string(int result);

/**
 * Check if the server has started shutting down
 *
 * \return 1 if the server is shutting down
 */
int slapi_is_shutting_down(void);

/*
 * checking routines for allocating and freeing memory
 */
char *slapi_ch_malloc(unsigned long size) __ATTRIBUTE__((returns_nonnull));
/*
 * memalign returns an alligned block of memory as a multiple of alignment.
 * alignment must be a power of 2. This is not normally needed, but is required
 * for memory that works with certain cpu operations. It's basically malloc
 * with some extra guarantees.
 *
 * \param size The size of the memory to allocate
 * \param alignment The alignment. MUST be a power of 2!
 * \return Pointer to the allocated memory aligned by alignment.
 */
char *slapi_ch_memalign(uint32_t size, uint32_t alignment) __ATTRIBUTE__((returns_nonnull));
char *slapi_ch_realloc(char *block, unsigned long size) __ATTRIBUTE__((returns_nonnull));
char *slapi_ch_calloc(unsigned long nelem, unsigned long size) __ATTRIBUTE__((returns_nonnull));
char *slapi_ch_strdup(const char *s) __ATTRIBUTE__((returns_nonnull));
void slapi_ch_free(void **ptr);
void slapi_ch_free_string(char **s);
struct berval *slapi_ch_bvdup(const struct berval *);
struct berval **slapi_ch_bvecdup(struct berval **);
void slapi_ch_bvfree(struct berval **v);
char *slapi_ch_smprintf(const char *fmt, ...) __ATTRIBUTE__((format(printf, 1, 2)));
/**
 * slapi_ct_memcmp is a constant time memory comparison function. This is for
 * use with password hashes and other locations which could lead to a timing
 * attack due to early shortcut returns. This function *does not* shortcircuit
 * during the comparison, always checking every byte regardless if it has already
 * found that the memory does not match.
 *
 * WARNING! p1 and p2 must both reference content that is at least of size 'n'.
 * Else this function may over-run (And will certainly fail).
 *
 * \param p1 pointer to first value to check.
 * \param p2 pointer to second value to check.
 * \param n length in bytes of the content of p1 AND p2.
 * \return 0 on match. 1 on non-match. 2 on presence of NULL pointer in p1 or p2.
 */
int slapi_ct_memcmp(const void *p1, const void *p2, size_t n);

/*
 * syntax plugin routines
 * THESE ARE DEPRECATED - the first argument is the syntax plugin
 * we do not support that style of call anymore - use the slapi_attr_
 * versions below instead
 */
int slapi_call_syntax_values2keys_sv(void *vpi, Slapi_Value **vals, Slapi_Value ***ivals, int ftype);
int slapi_call_syntax_values2keys_sv_pb(void *vpi, Slapi_Value **vals, Slapi_Value ***ivals, int ftype, Slapi_PBlock *pb);
int slapi_call_syntax_assertion2keys_ava_sv(void *vpi, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
int slapi_call_syntax_assertion2keys_sub_sv(void *vpi, char *initial, char **any, char * final, Slapi_Value ***ivals);
int slapi_call_syntax_assertion2keys_sub_sv_pb(Slapi_PBlock *pb, void *vpi, char *initial, char **any, char * final, Slapi_Value ***ivals);

int slapi_attr_values2keys_sv(const Slapi_Attr *sattr, Slapi_Value **vals, Slapi_Value ***ivals, int ftype);
int slapi_attr_values2keys_sv_pb(const Slapi_Attr *sattr, Slapi_Value **vals, Slapi_Value ***ivals, int ftype, Slapi_PBlock *pb);
int slapi_attr_assertion2keys_ava_sv(const Slapi_Attr *sattr, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
int slapi_attr_assertion2keys_sub_sv(const Slapi_Attr *sattr, char *initial, char **any, char * final, Slapi_Value ***ivals);
int slapi_attr_assertion2keys_sub_sv_pb(Slapi_PBlock *pb, const Slapi_Attr *sattr, char *initial, char **any, char * final, Slapi_Value ***ivals);

/**
 * Normalize the given value using the syntax associated with the
 * given attribute.  It will attempt to normalize the value in place.
 * If it cannot, it will return the normalized value in retval.  If
 * trim_spaces is true, whitepace characters will be trimmed from the
 * ends of the string.  If sattr is NULL, the type will be used to look
 * up the attribute syntax.  If sattr is not NULL, type is ignored.  If
 * retval is set, the caller is responsible for freeing it.
 *
 * \param pb Slapi_PBlock to use
 * \param sattr attribute to get the syntax from
 * \param type attribute to get the syntax from if sattr is NULL
 * \param val value to normalize in place - must be NULL terminated
 * \param trim_spaces trim whitespace from ends of string
 * \param retval if value could not be normalized in place, this is the malloc'd memory containg the new value - caller must free
 */
void slapi_attr_value_normalize(
    Slapi_PBlock *pb,
    const Slapi_Attr *sattr, /* if sattr is NULL, type must be attr type name */
    const char *type,
    char *val,
    int trim_spaces,
    char **retval);

/**
 * Normalize the given value using the matching rule associated with the
 * given attribute and given filter type.  It will attempt to normalize
 * the value in place. If it cannot, it will return the normalized value
 * in retval.  If trim_spaces is true, whitepace characters will be trimmed
 * from the ends of the string.  If sattr is NULL, the type will be used to look
 * up the attribute syntax.  If sattr is not NULL, type is ignored.  If
 * retval is set, the caller is responsible for freeing it.  The filter_type
 * corresponds to the matching rule to use - LDAP_FILTER_GE or LDAP_FILTER_LE
 * will use the ORDERING matching rule normalization function - LDAP_FILTER_EQUALITY
 * will use the EQUALITY matching rule normalization function - LDAP_FILTER_SUBSTRINGS
 * will use the SUBSTRINGS matching rule normalization function.  If the given
 * filter_type is 0, or some other value other than specified above, or there is no
 * matching rule corresponding to the given filter type, the default normalization
 * function provided by the attribute syntax will be used.
 *
 * \param pb Slapi_PBlock to use
 * \param sattr attribute to get the syntax from
 * \param type attribute to get the syntax from if sattr is NULL
 * \param val value to normalize in place - must be NULL terminated
 * \param trim_spaces trim whitespace from ends of string
 * \param retval if value could not be normalized in place, this is the malloc'd memory containg the new value - caller must free
 * \param filter_type one of the values specified above, or 0
 */
void slapi_attr_value_normalize_ext(
    Slapi_PBlock *pb,
    const Slapi_Attr *sattr, /* if sattr is NULL, type must be attr type name */
    const char *type,
    char *val,
    int trim_spaces,
    char **retval,
    unsigned long filter_type);

/*
 * internal operation and plugin callback routines
 */
typedef void (*plugin_result_callback)(int rc, void *callback_data);
typedef int (*plugin_referral_entry_callback)(char *referral,
                                              void *callback_data);
typedef int (*plugin_search_entry_callback)(Slapi_Entry *e,
                                            void *callback_data);
void slapi_free_search_results_internal(Slapi_PBlock *pb);


/*
 * The following functions can be used for internal operations based on DN
 * as well as on uniqueid. These functions should be used by all new plugins
 * and preferrably old plugins should be changed to use them to take
 * advantage of new plugin configuration capabilities and to use an
 * extensible interface.
 *
 * These functions return -1 if pb is NULL and 0 otherwise.
 * The SLAPI_PLUGIN_INTOP_RESULT pblock parameter should be checked to
 * check if the operation was successful.
 *
 * Helper functions are provided to set up pblock parameters currently used
 * by the functions, e.g., slapi_search_internal_set_pb().
 * Additional parameters may be set directly in the pblock.
 */

int slapi_search_internal_pb(Slapi_PBlock *pb);
int slapi_search_internal_callback_pb(Slapi_PBlock *pb, void *callback_data, plugin_result_callback prc, plugin_search_entry_callback psec, plugin_referral_entry_callback prec);
int slapi_add_internal_pb(Slapi_PBlock *pb);
int slapi_modify_internal_pb(Slapi_PBlock *pb);
int slapi_modrdn_internal_pb(Slapi_PBlock *pb);
int slapi_delete_internal_pb(Slapi_PBlock *pb);


int slapi_seq_internal_callback_pb(Slapi_PBlock *pb, void *callback_data, plugin_result_callback res_callback, plugin_search_entry_callback srch_callback, plugin_referral_entry_callback ref_callback);

void slapi_search_internal_set_pb(Slapi_PBlock *pb, const char *base, int scope, const char *filter, char **attrs, int attrsonly, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_search_internal_set_pb_ext(Slapi_PBlock *pb, Slapi_DN *sdn, int scope, const char *filter, char **attrs, int attrsonly, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_add_entry_internal_set_pb(Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **controls, Slapi_ComponentId *plugin_identity, int operation_flags);
int slapi_add_internal_set_pb(Slapi_PBlock *pb, const char *dn, LDAPMod **attrs, LDAPControl **controls, Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_modify_internal_set_pb(Slapi_PBlock *pb, const char *dn, LDAPMod **mods, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_modify_internal_set_pb_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, LDAPMod **mods, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags);
/**
 * Set \c Slapi_PBlock to perform modrdn/rename internally
 *
 * \param pblock - Slapi_PBlock to be set
 * \param olddn - original dn to be renamed; it should have been normalized
 * \param newrdn - new leaf rdn if any; it should have been normalized
 * \param newsuperior - new parent dn if any; it should have been normalized
 * \param deloldrdn \c 0 - keep original rdn \c non-zero - delete original rdn
 * \param controls
 * \param uniqueid
 * \param plugin_identity
 * \param operation_flags
 */
void slapi_rename_internal_set_pb_ext(Slapi_PBlock *pb,
                                      const Slapi_DN *olddn,
                                      const char *newrdn,
                                      const Slapi_DN *newsuperior,
                                      int deloldrdn,
                                      LDAPControl **controls,
                                      const char *uniqueid,
                                      Slapi_ComponentId *plugin_identity,
                                      int operation_flags);

void slapi_delete_internal_set_pb(Slapi_PBlock *pb, const char *dn, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_seq_internal_set_pb(Slapi_PBlock *pb, char *ibase, int type, char *attrname, char *val, char **attrs, int attrsonly, LDAPControl **controls, Slapi_ComponentId *plugin_identity, int operation_flags);

/*
 * slapi_search_internal_get_entry() finds an entry given a dn.  It returns
 * an LDAP error code (LDAP_SUCCESS if all goes well).  Caller must free ret_entry
 */
int slapi_search_internal_get_entry(Slapi_DN *dn, char **attrlist, Slapi_Entry **ret_entry, void *caller_identity);

/*
 * interface for registering object extensions.
 */
typedef void *(*slapi_extension_constructor_fnptr)(void *object, void *parent);

typedef void (*slapi_extension_destructor_fnptr)(void *extension,
                                                 void *object,
                                                 void *parent);

int slapi_register_object_extension(const char *pluginname,
                                    const char *objectname,
                                    slapi_extension_constructor_fnptr constructor,
                                    slapi_extension_destructor_fnptr destructor,
                                    int *objecttype,
                                    int *extensionhandle);

int
slapi_unregister_object_extension(
    const char *pluginname,
    const char *objectname,
    int *objecttype,
    int *ext_index);

/* objects that can be extended (possible values for the objectname param.) */
#define SLAPI_EXT_CONNECTION "Connection"
#define SLAPI_EXT_OPERATION  "Operation"
#define SLAPI_EXT_ENTRY      "Entry"
#define SLAPI_EXT_MTNODE     "Mapping Tree Node"

/* Module names for SLAPI_EXT_ENTRY */
#define SLAPI_EXTMOD_PWPOLICY "Password Policy"

/* Object names in SLAPI_EXT_ENTRY */
#define SLAPI_EXTOBJ_ENTRY_PW "UnhashedPW"

void *slapi_get_object_extension(int objecttype, void *object, int extensionhandle);
void slapi_set_object_extension(int objecttype, void *object, int extensionhandle, void *extension);

/*
 * interface to allow a plugin to register additional plugins.
 */
typedef int (*slapi_plugin_init_fnptr)(Slapi_PBlock *pb);
int slapi_register_plugin(const char *plugintype, int enabled, const char *initsymbol, slapi_plugin_init_fnptr initfunc, const char *name, char **argv, void *group_identity);

int slapi_register_plugin_ext(const char *plugintype, int enabled, const char *initsymbol, slapi_plugin_init_fnptr initfunc, const char *name, char **argv, void *group_identity, int precedence);

/*
 * logging
 */
#ifdef _SLDAPD_H_
/*
 * Use modern definition that avoid complaint about removing const from string literal
 * ( Note: this is binary compatible with old API definition so no problem here )
 */
int slapi_log_error(int loglevel, const char *subsystem, const char *fmt, ...) __ATTRIBUTE__((format(printf, 3, 4)));

int slapi_log_error_ext(int loglevel, const char *subsystem, const char *fmt, va_list varg1, va_list varg2);
#else
/* Use the old legacy definition because some external tester redefine these functions
 * and changing the prototype would break their compilation
 */
int slapi_log_error(int loglevel, char *subsystem, char *fmt, ...) __ATTRIBUTE__((format(printf, 3, 4)));

int slapi_log_error_ext(int loglevel, char *subsystem, char *fmt, va_list varg1, va_list varg2);
#endif

/* allowed values for the "severity" parameter */
#define SLAPI_LOG_FATAL       0
#define SLAPI_LOG_TRACE       1
#define SLAPI_LOG_PACKETS     2
#define SLAPI_LOG_ARGS        3
#define SLAPI_LOG_CONNS       4
#define SLAPI_LOG_BER         5
#define SLAPI_LOG_FILTER      6
#define SLAPI_LOG_CONFIG      7
#define SLAPI_LOG_ACL         8
#define SLAPI_LOG_SHELL       9
#define SLAPI_LOG_PARSE      10
#define SLAPI_LOG_HOUSE      11
#define SLAPI_LOG_REPL       12
#define SLAPI_LOG_CACHE      13
#define SLAPI_LOG_PLUGIN     14
#define SLAPI_LOG_TIMING     15
#define SLAPI_LOG_BACKLDBM   16
#define SLAPI_LOG_ACLSUMMARY 17
#define SLAPI_LOG_PWDPOLICY  18
/* Severity levels */
#define SLAPI_LOG_EMERG      19
#define SLAPI_LOG_ALERT      20
#define SLAPI_LOG_CRIT       21
#define SLAPI_LOG_ERR        22
#define SLAPI_LOG_WARNING    23
#define SLAPI_LOG_NOTICE     24
#define SLAPI_LOG_INFO       25
#define SLAPI_LOG_DEBUG      26 /* The last level must be set in log.c: SLAPI_LOG_MAX */

int slapi_is_loglevel_set(const int loglevel);


/*
 * locks and synchronization
 */
typedef struct slapi_mutex Slapi_Mutex;
typedef struct slapi_condvar Slapi_CondVar;
#ifdef USE_POSIX_RWLOCKS
typedef pthread_rwlock_t Slapi_RWLock;
#else
typedef PRRWLock Slapi_RWLock;
#endif
Slapi_Mutex *slapi_new_mutex(void);
void slapi_destroy_mutex(Slapi_Mutex *mutex);
void slapi_lock_mutex(Slapi_Mutex *mutex);
int slapi_unlock_mutex(Slapi_Mutex *mutex);
Slapi_CondVar *slapi_new_condvar(Slapi_Mutex *mutex);
void slapi_destroy_condvar(Slapi_CondVar *cvar);
int slapi_wait_condvar(Slapi_CondVar *cvar, struct timeval *timeout) __attribute__((deprecated));
int slapi_notify_condvar(Slapi_CondVar *cvar, int notify_all);
int slapi_wait_condvar_pt(Slapi_CondVar *cvar, Slapi_Mutex *mutex, struct timeval *timeout);

/**
 * Creates a new read/write lock
 * If prio_writer the rwlock gives priority on writers
 * else it give priority on readers (default)
 *
 * \return A pointer to a \c Slapi_RWLock
 *
 * \note Free the returned lock by calling slapi_destroy_rwlock() when finished
 *
 * \see slapi_destroy_rwlock()
 * \see slapi_rwlock_rdlock()
 * \see slapi_rwlock_wrlock()
 * \see slapi_rwlock_unlock()
 */
Slapi_RWLock *slapi_new_rwlock_prio(int32_t prio_writer);

/**
 * Creates a new read/write lock.
 *
 * \return A pointer to a \c Slapi_RWLock
 *
 * \note Free the returned lock by calling slapi_destroy_rwlock() when finished
 *
 * \see slapi_destroy_rwlock()
 * \see slapi_rwlock_rdlock()
 * \see slapi_rwlock_wrlock()
 * \see slapi_rwlock_unlock()
 */
Slapi_RWLock *slapi_new_rwlock(void);

/**
 * Free a read/write lock
 *
 * \see slapi_new_rwlock()
 */
void slapi_destroy_rwlock(Slapi_RWLock *rwlock);

/**
 * Acquires a reader lock
 *
 * \return 0 if successful
 * \return Non-zero if an error occurred
 *
 * \note Release the acquired lock by calling slapi_rwlock_unlock()
 * \note It is safe for a thread to hold multiple reader locks, but
 *       you must call slapi_rwlock_unlock() for each aquired lock.
 *
 * \see slapi_rwlock_unlock()
 */
int slapi_rwlock_rdlock(Slapi_RWLock *rwlock);

/**
 * Acquires a writer lock
 *
 * \return 0 if successful
 * \return Non-zero if an error occurred
 *
 * \note Release the acquired lock by calling slapi_rwlock_unlock()
 *
 * \see slapi_rwlock_unlock()
 */
int slapi_rwlock_wrlock(Slapi_RWLock *rwlock);

/**
 * Releases a reader or writer lock
 *
 * \return 0 if successful
 * \return Non-zero if an error occurred
 *
 * \see slapi_rwlock_rdlock()
 * \see slapi_rwlock_wrlock()
 */
int slapi_rwlock_unlock(Slapi_RWLock *rwlock);

/**
 * Get the size of Slapi_RWLock
 *
 * \return the size of Slapi_RWLock
 */
int slapi_rwlock_get_size(void);

/*
 * thread-safe LDAP connections
 */
#define SLAPI_LDAP_INIT_FLAG_SSL      1 /* SSL */
#define SLAPI_LDAP_INIT_FLAG_startTLS 2 /* startTLS */
/**
 * Initializes an LDAP connection, and returns a handle to the connection.
 *
 * \param ldaphost Hostname or IP address - NOTE: for TLS or GSSAPI, should be the FQDN
 * \param ldapport LDAP server port number (default 389)
 * \param secure \c 0 - LDAP \c SLAPI_LDAP_INIT_FLAG_SSL - LDAPS \c SLAPI_LDAP_INIT_FLAG_startTLS - startTLS
 * \param shared \c 0 - single thread access \c 1 - LDAP* will be shared among multiple threads
 * \return A pointer to an LDAP* handle
 *
 * \note Use #slapi_ldap_unbind() to close and free the handle
 *
 * \see slapi_ldap_unbind()
 * \see slapi_ldap_init_ext()
 */
LDAP *slapi_ldap_init(char *ldaphost, int ldapport, int secure, int shared);
/**
 * Closes an LDAP connection, and frees memory associated with the handle
 *
 * \param ld the LDAP connection handle
 *
 * \see slapi_ldap_init()
 * \see slapi_ldap_init_ext()
 */
void slapi_ldap_unbind(LDAP *ld);

/**
 * Initializes an LDAP connection, and returns a handle to the connection.
 *
 * \param ldapurl A full LDAP url in the form ldap[s]://hostname:port or
 *                ldapi://path - if \c NULL, #hostname, #port, and #secure must be provided
 * \param hostname Hostname or IP address - NOTE: for TLS or GSSAPI, should be the FQDN
 * \param port LDAP server port number (default 389)
 * \param secure \c 0 - LDAP \c SLAPI_LDAP_INIT_FLAG_SSL - LDAPS \c SLAPI_LDAP_INIT_FLAG_startTLS - startTLS
 * \param shared \c 0 - single thread access \c 1 - LDAP* will be shared among multiple threads
 * \param ldapi_socket - ldapi socket path
 * \return A pointer to an LDAP* handle
 *
 * \note Use #slapi_ldap_unbind() to close and free the handle
 *
 * \see slapi_ldap_unbind()
 * \see slapi_ldap_init()
 */
LDAP *slapi_ldap_init_ext(
    const char *ldapurl,    /* full ldap url */
    const char *hostname,   /* can also use this to override
                             host in url */
    int port,               /* can also use this to override port in url */
    int secure,             /* 0 for ldap, 1 for ldaps, 2 for starttls -
                   override proto in url */
    int shared,             /* if true, LDAP* will be shared among multiple threads */
    const char *ldap_socket /* ldapi socket path */
    );
/**
 * The LDAP bind request - this function handles all of the different types of mechanisms
 * including simple, sasl, and external (client cert auth)
 *
 * \param ld the LDAP connection handle
 * \param bindid Either a bind DN for simple bind or a SASL identity
 * \param creds usually a password for simple bind or SASL credentials
 * \param mech a valid mech that can be passed to ldap_sasl_bind (or NULL for simple)
 * \param serverctrls optional controls to send (or NULL)
 * \param returnedctrls optional controls returned by the server - use NULL if you just
 *                      want to ignore them - if you pass in a variable for this, you
 *                      are responsible for freeing the result (ldap_controls_free)
 * \param timeout timeout or NULL for no timeout (wait forever)
 * \param msgidp LDAP message ID \c NULL - call bind synchronously \c non-NULL -
 *               call bind asynchronously - you are responsible for calling ldap_result
 *               to read the response
 * \return an LDAP error code
 *
 * \see ldap_sasl_bind()
 * \see ldap_sasl_bind_s()
 * \see ldap_controls_free()
 * \see ldap_result()
 */
int slapi_ldap_bind(
    LDAP *ld,                     /* ldap connection */
    const char *bindid,           /* usually a bind DN for simple bind */
    const char *creds,            /* usually a password for simple bind */
    const char *mech,             /* name of mechanism */
    LDAPControl **serverctrls,    /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    struct timeval *timeout,      /* timeout */
    int *msgidp                   /* pass in non-NULL for async handling */
    );

/**
 * Return the full path of PEM format CA Cert
 *
 * \return the full path of PEM format CA Cert
 */
const char *slapi_get_cacertfile(void);

/**
 * Set the full path of PEM format CA Cert
 */
void slapi_set_cacertfile(char *certfile);

/**
 * Create either a v1 Proxy Auth Control or a v2 Proxied Auth Control
 *
 * \param ld the LDAP connection handle
 * \param dn The proxy DN
 * \param creds usually a password for simple bind or SASL credentials
 * \param ctl_iscritical \c 0 - not critical \c 1 - critical
 * \param usev2 \c 0 - use the v1 Proxy Auth \c 1 - use the v2 Proxied Auth
 * \param ctrlp the control to send - caller is responsible for freeing (ldap_control_free)
 * \return an LDAP error code
 *
 * \see ldap_control_free()
 */
int slapi_ldap_create_proxyauth_control(
    LDAP *ld,       /* only used to get current ber options */
    const char *dn, /* proxy dn */
    const char ctl_iscritical,
    int usev2,          /* use the v2 (.18) control instead */
    LDAPControl **ctrlp /* value to return */
    );

/**
 * Parse a line from an LDIF record returned by ldif_getline() and return the LDAP
 * attribute type and value from the line.  ldif_getline() will encode the LDIF continuation
 * lines, and slapi_ldif_parse_line() will take those into consideration when returning the
 * value.
 *
 * \param line LDIF record line returned by ldif_getline()
 * \param type The attribute type returned
 * \param value The attribute value returned
 * \param freeval \c NULL - use malloc for the value->bv_val - caller is responsible for freeing
 *                \c an int* - slapi_ldif_parse_line will usually return pointers into the line
 *                parameter that should not be freed - if slapi_ldif_parse_line needs to allocate
 *                memory for the value, *freeval will be set to 1 to indicate the caller must
 *                free value->bv_val
 * \return \c 0 - success \c 1 - failure
 *
 * \note line is written to - you must pass in writable memory - line must be NULL terminated
 *
 * \see ldif_getline()
 */
int slapi_ldif_parse_line(
    char *line,           /* line to parse */
    struct berval *type,  /* attribute type to return */
    struct berval *value, /* attribute value to return */
    int *freeval          /* values will usually be returned in place as pointers into line - if the value is a url, the value will be malloced and must be freed by the caller */
    );

/**
 * Parse an LDAP DN string.  Return an array of RDN strings, terminated by a NULL.  This
 * function differs from the standard openldap ldap_explode_dn, which will escape utf-8
 * characters.  In the directory server, we do not want to escape them.  The caller
 * should use slapi_ldap_value_free to free the returned memory when finished.
 *
 * \param dn      The LDAP DN
 * \param notypes set to true (1) to return only the attribute values with no attribute types
 * \return \c     An array of RDN strings - use slapi_ch_array_free to free
 *
 * \see slapi_ldap_value_free()
 */
char **slapi_ldap_explode_dn(
    const char *dn, /* dn to explode */
    int notypes     /* set to true to return only the values with no types */
    );

/**
 * Parse an LDAP RDN string.  Return an array of AVA strings, terminated by a NULL.  This
 * function differs from the standard openldap ldap_explode_rdn, which will escape utf-8
 * characters.  In the directory server, we do not want to escape them.  The caller
 * should use slapi_ldap_value_free to free the returned memory when finished.
 *
 * \param dn      The LDAP RDN
 * \param notypes set to true (1) to return only the attribute values with no attribute types
 * \return \c     An array of AVA strings - use slapi_ch_array_free to free
 *
 * \see slapi_ldap_value_free()
 */
char **slapi_ldap_explode_rdn(
    const char *rdn, /* rdn to explode */
    int notypes      /* set to true to return only the values with no types */
    );

/*
 * computed attributes
 */
struct _computed_attr_context;
typedef struct _computed_attr_context computed_attr_context;
typedef int (*slapi_compute_output_t)(computed_attr_context *c, Slapi_Attr *a, Slapi_Entry *e);
typedef int (*slapi_compute_callback_t)(computed_attr_context *c, char *type, Slapi_Entry *e, slapi_compute_output_t outputfn);
typedef enum slapi_compute_callback_result {
    COMPUTE_CALLBACK_CONTINUE = -1,
    COMPUTE_CALLBACK_DONE,
} slapi_compute_callback_result_t;

typedef int (*slapi_search_rewrite_callback_t)(Slapi_PBlock *pb);
typedef enum slapi_search_rewrite_callback_result {
    SEARCH_REWRITE_CALLBACK_CONTINUE = -1,
    SEARCH_REWRITE_CALLBACK_DONE,
    SEARCH_REWRITE_CALLBACK_REFUSE,
    SEARCH_REWRITE_CALLBACK_ERROR,
} slapi_search_rewrite_callback_result_t;
int slapi_compute_add_evaluator(slapi_compute_callback_t function);
int slapi_compute_add_evaluator_ext(slapi_compute_callback_t function, int rootonly);
int slapi_compute_add_search_rewriter(slapi_search_rewrite_callback_t function);
int compute_rewrite_search_filter(Slapi_PBlock *pb);


/*
 * routines for dealing with backends
 */
Slapi_Backend *slapi_be_new(const char *type, const char *name, int isprivate, int logchanges);
void slapi_be_free(Slapi_Backend **be);
Slapi_Backend *slapi_be_select(const Slapi_DN *sdn);
Slapi_Backend *slapi_be_select_exact(const Slapi_DN *sdn);
Slapi_Backend *slapi_be_select_by_instance_name(const char *name);
int slapi_be_exist(const Slapi_DN *sdn);
void slapi_be_delete_onexit(Slapi_Backend *be);
void slapi_be_set_readonly(Slapi_Backend *be, int readonly);
int slapi_be_get_readonly(Slapi_Backend *be);
int slapi_be_getentrypoint(Slapi_Backend *be, int entrypoint, void **ret_fnptr, Slapi_PBlock *pb);
int slapi_be_setentrypoint(Slapi_Backend *be, int entrypoint, void *ret_fnptr, Slapi_PBlock *pb);
int slapi_be_logchanges(Slapi_Backend *be);
int slapi_be_issuffix(const Slapi_Backend *be, const Slapi_DN *suffix);
void slapi_be_addsuffix(Slapi_Backend *be, const Slapi_DN *suffix);
char *slapi_be_get_name(Slapi_Backend *be);
const Slapi_DN *slapi_be_getsuffix(Slapi_Backend *be, int n);
Slapi_Backend *slapi_get_first_backend(char **cookie);
Slapi_Backend *slapi_get_next_backend(char *cookie);
int slapi_be_private(Slapi_Backend *be);
void *slapi_be_get_instance_info(Slapi_Backend *be);
void slapi_be_set_instance_info(Slapi_Backend *be, void *data);
Slapi_DN *slapi_get_first_suffix(void **node, int show_private);
Slapi_DN *slapi_get_next_suffix(void **node, int show_private);
Slapi_DN *slapi_get_next_suffix_ext(void **node, int show_private);
int slapi_is_root_suffix(Slapi_DN *dn);
const Slapi_DN *slapi_get_suffix_by_dn(const Slapi_DN *dn);
const char *slapi_be_gettype(Slapi_Backend *be);
int slapi_exist_referral(Slapi_Backend *be);
void slapi_referral_check_init(void);
void slapi_referral_check_stop(void);

/**
 * Start database transaction
 *
 * \param pb Pblock which is supposed to set (Slapi_Backend *) to SLAPI_BACKEND
 * \return 0 if successful
 * \return SLAPI_BACK_TRANSACTION_NOT_SUPPORTED if transaction support is not available for this backend
 * \return Non-zero if an error occurred
 *
 * \see slapi_back_transaction_commit
 * \see slapi_back_transaction_abort
 */
int slapi_back_transaction_begin(Slapi_PBlock *pb);
/**
 * Commit database transaction
 *
 * \param pb Pblock which is used to start transaction
 * \return 0 if successful
 * \return Non-zero if an error occurred
 *
 * \see slapi_back_transaction_start
 * \see slapi_back_transaction_abort
 */
int slapi_back_transaction_commit(Slapi_PBlock *pb);
/**
 * Abort database transaction
 *
 * \param pb Pblock which is used to start transaction
 * \return 0 if successful
 * \return Non-zero if an error occurred
 *
 * \see slapi_back_transaction_commit
 * \see slapi_back_transaction_abort
 */
int slapi_back_transaction_abort(Slapi_PBlock *pb);

/**
 * Checks if the connection type is LDAPI
 *
 * \param pb Parameter block.
 *
 * \return \c 1 if connection type is LDAPI
 * \return \c 0 if connection is not LDAPI
 */
int slapi_is_ldapi_conn(Slapi_PBlock *pb);

int slapi_be_is_flag_set(Slapi_Backend *be, int flag);
void slapi_be_set_flag(Slapi_Backend *be, int flag);
void slapi_be_unset_flag(Slapi_Backend *be, int flag);
#define SLAPI_BE_FLAG_REMOTE_DATA              0x1  /* entries held by backend are remote */
#define SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST  0x10  /* force to call filter_test (search only) */
#define SLAPI_BE_FLAG_POST_IMPORT            0x100  /* backend was imported */
#define SLAPI_BE_FLAG_POST_RESTORE           0x200  /* startup after restore */
#define SLAPI_BE_FLAG_CONTAINS_REFERRAL      0x400  /* Used to flag that the backend contains a referral entry */


/* These functions allow a plugin to register for callback when
 * a backend state change
 */
typedef void (*slapi_backend_state_change_fnptr)(void *handle, char *be_name, int old_be_state, int new_be_state);
void slapi_register_backend_state_change(void *handle, slapi_backend_state_change_fnptr funct);
int slapi_unregister_backend_state_change(void *handle);
#define SLAPI_BE_STATE_ON      1  /* backend is ON */
#define SLAPI_BE_STATE_OFFLINE 2  /* backend is OFFLINE (import process) */
#define SLAPI_BE_STATE_DELETE  3  /* backend has been deleted */

/*
 * Distribution.
 */
/* SLAPI_BE_ALL_BACKENDS is a special value that is returned by
 * a distribution plugin function to indicate that all backends
 * should be searched (it is only used for search operations).
 *
 * SLAPI_BE_NO_BACKEND indicates no more backend is available or allowed
 * SLAPI_BE_REMOTE_BACKEND indicates that the are not locally available
 */
#define SLAPI_BE_ALL_BACKENDS   -1
#define SLAPI_BE_NO_BACKEND     -2
#define SLAPI_BE_REMOTE_BACKEND -3


/*
 * virtual attribute service
 */

/* General flags (flags parameter) */
#define SLAPI_REALATTRS_ONLY                       1
#define SLAPI_VIRTUALATTRS_ONLY                    2
#define SLAPI_VIRTUALATTRS_REQUEST_POINTERS        4  /* I want to receive pointers into the entry, if possible */
#define SLAPI_VIRTUALATTRS_LIST_OPERATIONAL_ATTRS  8  /* Include operational attributes in attribute lists */
#define SLAPI_VIRTUALATTRS_SUPPRESS_SUBTYPES      16  /* I want only the requested attribute */

/* Buffer disposition flags (buffer_flags parameter) */
#define SLAPI_VIRTUALATTRS_RETURNED_POINTERS 1
#define SLAPI_VIRTUALATTRS_RETURNED_COPIES   2
#define SLAPI_VIRTUALATTRS_REALATTRS_ONLY    4
#define SLAPI_VIRTUALATTRS_VALUES_CACHEABLE  8

/* Attribute type name disposition values (type_name_disposition parameter) */
#define SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS 1
#define SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE          2
#define SLAPI_VIRTUALATTRS_NOT_FOUND     -1
#define SLAPI_VIRTUALATTRS_LOOP_DETECTED -2

typedef struct _vattr_type_thang vattr_type_thang;
typedef struct _vattr_get_thang vattr_get_thang;
vattr_get_thang *slapi_vattr_getthang_first(vattr_get_thang *t);
vattr_get_thang *slapi_vattr_getthang_next(vattr_get_thang *t);

int slapi_vattr_values_type_thang_get(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* attr type */ vattr_type_thang *type_thang,
    /* pointer to result set */ Slapi_ValueSet **results,
    int *type_name_disposition,
    char **actual_type_name,
    int flags,
    int *buffer_flags);
int slapi_vattr_values_get(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* attr type name */ char *type,
    /* pointer to result set */ Slapi_ValueSet **results,
    int *type_name_disposition,
    char **actual_type_name,
    int flags,
    int *buffer_flags);
int slapi_vattr_values_get_ex(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* attr type name */ char *type,
    /* pointer to result set */ Slapi_ValueSet ***results,
    int **type_name_disposition,
    char ***actual_type_name,
    int flags,
    int *buffer_flags,
    int *subtype_count);
int slapi_vattr_namespace_values_get(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* backend namespace dn */ Slapi_DN *namespace_dn,
    /* attr type name */ char *type,
    /* pointer to result set */ Slapi_ValueSet ***results,
    int **type_name_disposition,
    char ***actual_type_name,
    int flags,
    int *buffer_flags,
    int *subtype_count);
void slapi_vattr_values_free(Slapi_ValueSet **value, char **actual_type_name, int flags);
int slapi_vattr_value_compare(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* attr type name */ char *type,
    Slapi_Value *test_this,
    /* pointer to result */ int *result,
    int flags);
int slapi_vattr_namespace_value_compare(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* backend namespace dn */ Slapi_DN *namespace_dn,
    /* attr type name */ const char *type,
    Slapi_Value *test_this,
    /* pointer to result */ int *result,
    int flags);
int slapi_vattr_list_attrs(
    /* Entry we're interested in */ Slapi_Entry *e,
    /* pointer to receive the list */ vattr_type_thang **types,
    int flags,
    int *buffer_flags);
void slapi_vattr_attrs_free(vattr_type_thang **types, int flags);
char *vattr_typethang_get_name(vattr_type_thang *t);
unsigned long vattr_typethang_get_flags(vattr_type_thang *t);
vattr_type_thang *vattr_typethang_next(vattr_type_thang *t);
vattr_type_thang *vattr_typethang_first(vattr_type_thang *t);
int slapi_vattr_schema_check_type(Slapi_Entry *e, char *type);


/* roles */
typedef int (*roles_check_fn_type)(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present);

int slapi_role_check(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present);
void slapi_register_role_check(roles_check_fn_type check_fn);

/* DSE */
/* Front end configuration */
typedef int (*dseCallbackFn)(Slapi_PBlock *, Slapi_Entry *, Slapi_Entry *, int *, char *, void *);

/*
 * Note: DSE callback functions MUST return one of these three values:
 *
 *   SLAPI_DSE_CALLBACK_OK           -- no errors occurred; apply changes.
 *   SLAPI_DSE_CALLBACK_ERROR        -- an error occurred; don't apply changes.
 *   SLAPI_DSE_CALLBACK_DO_NOT_APPLY -- no error, but do not apply changes.
 *
 * SLAPI_DSE_CALLBACK_DO_NOT_APPLY should only be returned by modify
 * callbacks (i.e., those registered with operation==SLAPI_OPERATION_MODIFY).
 * A return value of SLAPI_DSE_CALLBACK_DO_NOT_APPLY is treated the same as
 * SLAPI_DSE_CALLBACK_ERROR for all other operations.
 */
#define SLAPI_DSE_CALLBACK_OK           (1)
#define SLAPI_DSE_CALLBACK_ERROR        (-1)
#define SLAPI_DSE_CALLBACK_DO_NOT_APPLY (0)

/*
 * Flags for slapi_config_register_callback() and
 *        slapi_config_remove_callback()
 */
#define DSE_FLAG_PREOP  0x0001
#define DSE_FLAG_POSTOP 0x0002
#define DSE_FLAG_PLUGIN 0x0004

/* This is the size of the returntext parameter passed to the config callback function,
   which is the "char *" argument to dseCallbackFn above */
#define SLAPI_DSE_RETURNTEXT_SIZE 512 /* for use by callback functions */

int slapi_config_register_callback(int operation, int flags, const char *base, int scope, const char *filter, dseCallbackFn fn, void *fn_arg);
int slapi_config_register_callback_plugin(int operation, int flags, const char *base, int scope, const char *filter, dseCallbackFn fn, void *fn_arg, Slapi_PBlock *pb);
int slapi_config_remove_callback(int operation, int flags, const char *base, int scope, const char *filter, dseCallbackFn fn);

/******************************************************************************
 * Online tasks interface (to support import, export, etc)
 * After some cleanup, we could consider making these public.
 */

/* task states */
#define SLAPI_TASK_SETUP     0
#define SLAPI_TASK_RUNNING   1
#define SLAPI_TASK_FINISHED  2
#define SLAPI_TASK_CANCELLED 3

/* task flag (pb_task_flags)*/
#define SLAPI_TASK_RUNNING_AS_TASK          0x0
#define SLAPI_TASK_RUNNING_FROM_COMMANDLINE 0x1

/* task flags (set by the task-control code) */
#define SLAPI_TASK_DESTROYING 0x01 /* queued event for destruction */

/* task warnings */
typedef enum task_warning_t{
    WARN_UPGRADE_DN_FORMAT_ALL    = (1 << 0),
    WARN_UPGRADE_DN_FORMAT        = (1 << 1),
    WARN_UPGRADE_DN_FORMAT_SPACE  = (1 << 2),
    WARN_SKIPPED_IMPORT_ENTRY     = (1 << 3)
} task_warning;


int slapi_task_register_handler(const char *name, dseCallbackFn func);
int slapi_plugin_task_register_handler(const char *name, dseCallbackFn func, Slapi_PBlock *plugin_pb);
int slapi_plugin_task_unregister_handler(const char *name, dseCallbackFn func);
void slapi_task_begin(Slapi_Task *task, int total_work);
void slapi_task_inc_progress(Slapi_Task *task);
void slapi_task_finish(Slapi_Task *task, int rc);
void slapi_task_cancel(Slapi_Task *task, int rc);
int slapi_task_get_state(Slapi_Task *task);
void slapi_task_set_data(Slapi_Task *task, void *data);
void *slapi_task_get_data(Slapi_Task *task);
void slapi_task_inc_refcount(Slapi_Task *task);
void slapi_task_dec_refcount(Slapi_Task *task);
int slapi_task_get_refcount(Slapi_Task *task);
void slapi_task_set_destructor_fn(Slapi_Task *task, TaskCallbackFn func);
void slapi_task_set_cancel_fn(Slapi_Task *task, TaskCallbackFn func);
void slapi_task_status_changed(Slapi_Task *task);
void slapi_task_set_warning(Slapi_Task *task, task_warning warn);
int slapi_task_get_warning(Slapi_Task *task);
void slapi_task_log_status(Slapi_Task *task, char *format, ...) __ATTRIBUTE__((format(printf, 2, 3)));

void slapi_task_log_notice(Slapi_Task *task, const char *format, ...) __ATTRIBUTE__((format(printf, 2, 3)));
void slapi_task_log_status_ext(Slapi_Task *task, char *format, va_list varg);
void slapi_task_log_notice_ext(Slapi_Task *task, char *format, va_list varg);

/*
 * slapi_new_task: create new task, fill in DN, and setup modify callback
 * argument:
 *     dn: task dn
 * result:
 *     Success: Slapi_Task object
 *     Failure: NULL
 */
Slapi_Task *slapi_new_task(const char *dn);

/**
 * slapi_plugin_new_task: create a new task from within a plugin, where
 *                        the task runs inside a thread.  This
 *
 * \param dn Task DN.
 * \param arg The function argument passed to the registered task function.
 *
 * \return Slapi_Task object;
 */
Slapi_Task *slapi_plugin_new_task(const char *dn, void *arg);

/* slapi_destroy_task: destroy a task
 * argument:
 *     task: task to destroy
 * result:
 *     none
 */
void slapi_destroy_task(void *arg);
/* End of interface to support online tasks **********************************/

/* Slapi_Counter Interface */
Slapi_Counter *slapi_counter_new(void);
void slapi_counter_init(Slapi_Counter *counter);
void slapi_counter_destroy(Slapi_Counter **counter);
uint64_t slapi_counter_increment(Slapi_Counter *counter);
uint64_t slapi_counter_decrement(Slapi_Counter *counter);
uint64_t slapi_counter_add(Slapi_Counter *counter, uint64_t addvalue);
uint64_t slapi_counter_subtract(Slapi_Counter *counter, uint64_t subvalue);
uint64_t slapi_counter_set_value(Slapi_Counter *counter, uint64_t newvalue);
uint64_t slapi_counter_get_value(Slapi_Counter *counter);

/* Binder-based (connection centric) resource limits */
/*
 * Valid values for `type' parameter to slapi_reslimit_register().
 */
#define SLAPI_RESLIMIT_TYPE_INT 0

/*
 * Status codes returned by all functions.
 */
#define SLAPI_RESLIMIT_STATUS_SUCCESS        0 /* goodness */
#define SLAPI_RESLIMIT_STATUS_NOVALUE        1 /* no value is available */
#define SLAPI_RESLIMIT_STATUS_INIT_FAILURE   2 /* initialization failed */
#define SLAPI_RESLIMIT_STATUS_PARAM_ERROR    3 /* bad parameter */
#define SLAPI_RESLIMIT_STATUS_UNKNOWN_HANDLE 4 /* unregistered handle */
#define SLAPI_RESLIMIT_STATUS_INTERNAL_ERROR 5 /* unexpected error */

/*
 * Functions.
 */
int slapi_reslimit_register(int type, const char *attrname, int *handlep);
int slapi_reslimit_get_integer_limit(Slapi_Connection *conn, int handle, int *limitp);
/* END of Binder-based resource limits API */

/**
 * Returns the current time
 *
 * CRITICAL: This funciton is NOT THREAD SAFE. DO NOT USE IT.
 * You MUST use slapi_current_time_hr instead.
 *
 * \return The current time
 */
time_t slapi_current_time(void) __attribute__((deprecated));

/**
 * Get the system time and check for errors.  Return
 *
 * \param tp - a timespec struct where the system time is set
 * \return result code, upon success tp is set to the system time
 * as a clock in UTC timezone. This clock adjusts with ntp steps,
 * and should NOT be used for timer information.
 */
int32_t slapi_clock_gettime(struct timespec *tp);
/* 
 * slapi_clock_gettime should have better been called
 * slapi_clock_utc_gettime but sice the function pre-existed
 * we are just adding an alias (to avoid risking to break
 * some custom plugins)
 */
#define slapi_clock_utc_gettime slapi_clock_gettime

/**
 * Returns the current system time as a hr clock relative to uptime
 * This means the clock is not affected by timezones
 * which can normally cause issues with timers. Additionally, this
 * is a thread safe clock.
 *
 * \return timespec of the current relative system time.
 */
struct timespec slapi_current_time_hr(void);
/**
 * Returns the current system time as a hr clock
 *
 * \return timespec of the current monotonic time.
 */
struct timespec slapi_current_rel_time_hr(void);
/**
 * Returns the current system time as a hr clock
 *
 * \return time_t of the current monotonic time.
 */
time_t slapi_current_rel_time_t(void);
/**
 * Returns the current system time as a hr clock in UTC timezone.
 * This clock adjusts with ntp steps, and should NOT be
 * used for timer information.
 *
 * \return timespec of the current UTC time.
 */
struct timespec slapi_current_utc_time_hr(void);
/**
 * Returns the current system time as a clock in UTC timezone.
 * This clock adjusts with ntp steps, and should NOT be
 * used for timer information.
 *
 * \return time_t of the current UTC time.
 */
time_t slapi_current_utc_time(void);

#define SLAPI_TIMESTAMP_BUFSIZE 32
/**
 * Populates a buffer with a timestamp formatted correctly for
 * directory servers usage. This generally means UTC from localsystem
 * time and in the format %Y%m%d%H%M%SZ.
 *
 * \param char *buf the buffer to populate.
 * \param size_t bufsize Size of the buffer to populate. Must be at least
 * SLAPI_TIMESTAMP_BUFSIZE
 */
void slapi_timestamp_utc_hr(char *buf, size_t bufsize);

typedef enum {
    TIMER_CONTINUE,
    TIMER_EXPIRED
} slapi_timer_result;

/**
 * populate a timespec with a time relative to now, that defines
 * an expiry. IE, current time + timeout. This wraps the internal
 * clock function to allow us to change clock operations without
 * changing large parts of the codebase.
 *
 * if a negative timeout is provided, we set expire to 0.
 *
 * \param timeout the number of seconds relative to now when
 * the timeout should occur.
 * \param struct timespec the struct to populate with the relative
 * time of when we should expire.
 */
void slapi_timespec_expire_at(time_t timeout, struct timespec *expire);

/**
 * populate a timespec with a time relative to start, that defines
 * an expiry. IE, start time + timeout.
 *
 * if a negative timeout is provided, we set expire to 0.
 *
 * \param timeout the number of seconds relative to now when
 * the timeout should occur.
 * \param struct timespec start the struct containing the start
 * time we are creating a relative expiry to.
 * \param struct timespec the struct to populate with the relative
 * time of when we should expire.
 */
void slapi_timespec_expire_rel(time_t timeout, struct timespec *start, struct timespec *expire);

/**
 * Given a slapi_operation, and a timeout, calculate the expiry
 * relative to the operations start time. This is used to determine
 * time outs from op start time + timeout. To check the timeout
 * you should call slapi_timespec_expire_check.
 *
 * \param Slapi_Operation o the operation to use.
 * \param time_t timeout the timeout in seconds.
 * \param struct timespec expiry the timespec that will be populated with
 * the system relative expiry time.
 */
void slapi_operation_time_expiry(Slapi_Operation *o, time_t timeout, struct timespec *expiry);

/**
 * check if a timespec has expired it's lifetime relative to now.
 * if the timespec is 0, we do not cause a timeout.
 *
 * \param timespec to check for validity.
 * \return slapi_time_result an enum of CONTINUE which means no
 * expiry has occured, or EXPIRED, to defined tht the time is
 * up. If timespec is 0, CONTINUE is always returned.
 */
slapi_timer_result slapi_timespec_expire_check(struct timespec *expire);


/*
 * Plugin and parameter block related macros (remainder of this file).
 */

/*
 * Plugin version.  Note that the Directory Server will load version 01
 * and 02 plugins, but some server features require 03 plugins.
 *
 * 2020-11-20 wbrown - The features in question for v03 only are related to
 * backend features - otherwise it doesn't matter. But for your peace of mind
 * just make everything version 3.
 */
#define SLAPI_PLUGIN_VERSION_01 "01"
#define SLAPI_PLUGIN_VERSION_02 "02"
#define SLAPI_PLUGIN_VERSION_03 "03"
#define SLAPI_PLUGIN_CURRENT_VERSION SLAPI_PLUGIN_VERSION_03
#define SLAPI_PLUGIN_IS_COMPAT(x)                   \
    ((strcmp((x), SLAPI_PLUGIN_VERSION_01) == 0) || \
     (strcmp((x), SLAPI_PLUGIN_VERSION_02) == 0) || \
     (strcmp((x), SLAPI_PLUGIN_VERSION_03) == 0))
#define SLAPI_PLUGIN_IS_V2(x)                                    \
    ((strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_02) == 0) || \
     (strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_03) == 0))
#define SLAPI_PLUGIN_IS_V3(x) \
    (strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_03) == 0)

/* this one just has to be human readable */
#define SLAPI_PLUGIN_SUPPORTED_VERSIONS "01,02,03"

/*
 * types of plugin interfaces
 */
#define SLAPI_PLUGIN_EXTENDEDOP                2
#define SLAPI_PLUGIN_PREOPERATION              3
#define SLAPI_PLUGIN_POSTOPERATION             4
#define SLAPI_PLUGIN_MATCHINGRULE              5
#define SLAPI_PLUGIN_SYNTAX                    6
#define SLAPI_PLUGIN_ACL                       7
#define SLAPI_PLUGIN_BEPREOPERATION            8
#define SLAPI_PLUGIN_BEPOSTOPERATION           9
#define SLAPI_PLUGIN_ENTRY                    10
#define SLAPI_PLUGIN_TYPE_OBJECT              11
#define SLAPI_PLUGIN_INTERNAL_PREOPERATION    12
#define SLAPI_PLUGIN_INTERNAL_POSTOPERATION   13
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME       14
#define SLAPI_PLUGIN_VATTR_SP                 15
#define SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME 16
#define SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE   17
#define SLAPI_PLUGIN_INDEX                    18
#define SLAPI_PLUGIN_BETXNPREOPERATION        19
#define SLAPI_PLUGIN_BETXNPOSTOPERATION       20
#define SLAPI_PLUGIN_BETXNEXTENDEDOP          21
#define SLAPI_PLUGIN_PREEXTOPERATION          22
#define SLAPI_PLUGIN_POSTEXTOPERATION         23
#define SLAPI_PLUGIN_MMR                      24

/*
 * special return values for extended operation plugins (zero or positive
 *     return values should be LDAP error codes as defined in ldap.h)
 */
#define SLAPI_PLUGIN_EXTENDED_SENT_RESULT          -1
#define SLAPI_PLUGIN_EXTENDED_NOT_HANDLED          -2
#define SLAPI_PLUGIN_EXTENDED_NO_BACKEND_AVAILABLE -3

/*
 * Return values of plugins:
 */
#define SLAPI_PLUGIN_SUCCESS 0  /* 0 is treated as success implicitely */
#define SLAPI_PLUGIN_FAILURE -1 /* common failure */
#define SLAPI_PLUGIN_NOOP    -2 /* Special in be_pre_op/be_txn_pre_op.
                                 * Treat as SUCCESS, but skip the backend op.
                                 * Also, return SUCCESS to the client/supplier.
                                 * Necessary for the replication conflicts. */
#define SLAPI_PLUGIN_NOOP_COMMIT -3 /* Special in be_pre_op/be_txn_pre_op.
                                     * main op should be noop, but plugin made
                                     * changes which need to be committed
                                     */
#define SLAPI_PLUGIN_NOOP_TOMBSTONE -4  /* Ignore the add and turn entry
                                         * into tombstone
                                         */

/*
 * the following can be used as the second argument to the
 * slapi_pblock_get() and slapi_pblock_set() calls.
 */

/* backend, connection, operation */
#define SLAPI_BACKEND          130
#define SLAPI_CONNECTION       131
#define SLAPI_OPERATION        132
#define SLAPI_REQUESTOR_ISROOT 133
#define SLAPI_BE_TYPE          135
#define SLAPI_BE_READONLY      136
#define SLAPI_BE_LASTMOD       137
#define SLAPI_CONN_ID          139
#define SLAPI_BACKEND_COUNT    860

/* operation */
#define SLAPI_OPINITIATED_TIME            140
#define SLAPI_REQUESTOR_DN                141
#define SLAPI_REQUESTOR_SDN               852
#define SLAPI_REQUESTOR_NDN               156
#define SLAPI_OPERATION_PARAMETERS        138
#define SLAPI_OPERATION_TYPE              590
#define SLAPI_OPERATION_AUTHTYPE          741
#define SLAPI_OPERATION_ID                744
#define SLAPI_OPERATION_SSF               750
#define SLAPI_IS_REPLICATED_OPERATION     142
#define SLAPI_IS_MMR_REPLICATED_OPERATION 153
#define SLAPI_SKIP_MODIFIED_ATTRS         155

/* connection */
#define SLAPI_CONN_DN                     143
#define SLAPI_CONN_CLIENTNETADDR          850
#define SLAPI_CONN_SERVERNETADDR          851
#define SLAPI_CONN_CLIENTNETADDR_ACLIP    853
#define SLAPI_CONN_IS_REPLICATION_SESSION 149
#define SLAPI_CONN_IS_SSL_SESSION         747
#define SLAPI_CONN_CERT                   743
#define SLAPI_CONN_AUTHMETHOD             746
#define SLAPI_CONN_SASL_SSF               748
#define SLAPI_CONN_SSL_SSF                749
#define SLAPI_CONN_LOCAL_SSF              751

/*
 * Types of authentication for SLAPI_CONN_AUTHMETHOD
 * (and deprecated SLAPI_CONN_AUTHTYPE)
 */
#define SLAPD_AUTH_NONE   "none"
#define SLAPD_AUTH_SIMPLE "simple"
#define SLAPD_AUTH_SSL    "SSL"
#define SLAPD_AUTH_SASL   "SASL " /* followed by the mechanism name */
#define SLAPD_AUTH_OS     "OS"

/* Command Line Arguments */
#define SLAPI_ARGC 147
#define SLAPI_ARGV 148

/* Slapd config file directory */
#define SLAPI_CONFIG_DIRECTORY 281

/* DSE flags */
#define SLAPI_DSE_DONT_WRITE_WHEN_ADDING 282
#define SLAPI_DSE_MERGE_WHEN_ADDING      283
#define SLAPI_DSE_DONT_CHECK_DUPS        284
#define SLAPI_DSE_REAPPLY_MODS           287
#define SLAPI_DSE_IS_PRIMARY_FILE        289

/* internal schema flags */
#define SLAPI_SCHEMA_FLAGS 285

/* urp flags */
#define SLAPI_URP_NAMING_COLLISION_DN 286
#define SLAPI_URP_TOMBSTONE_UNIQUEID  288
#define SLAPI_URP_TOMBSTONE_CONFLICT_DN	293

/* common to all plugins */
#define SLAPI_PLUGIN              3
#define SLAPI_PLUGIN_PRIVATE      4
#define SLAPI_PLUGIN_TYPE         5
#define SLAPI_PLUGIN_ARGV         6
#define SLAPI_PLUGIN_ARGC         7
#define SLAPI_PLUGIN_VERSION      8
#define SLAPI_PLUGIN_OPRETURN     9
#define SLAPI_PLUGIN_OBJECT      10
#define SLAPI_PLUGIN_DESTROY_FN  11
#define SLAPI_PLUGIN_DESCRIPTION 12
typedef struct slapi_plugindesc
{
    char *spd_id;
    char *spd_vendor;
    char *spd_version;
    char *spd_description;
} Slapi_PluginDesc;

#define SLAPI_PLUGIN_IDENTITY   13
#define SLAPI_PLUGIN_PRECEDENCE 14

/* common for internal plugin_ops */
#define SLAPI_PLUGIN_INTOP_RESULT           15
#define SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES   16
#define SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS 17

/* miscellaneous plugin functions */
#define SLAPI_PLUGIN_CLOSE_FN     210
#define SLAPI_PLUGIN_START_FN     212
#define SLAPI_PLUGIN_CLEANUP_FN   232
#define SLAPI_PLUGIN_POSTSTART_FN 233


/* extendedop plugin functions */
#define SLAPI_PLUGIN_EXT_OP_FN          300
#define SLAPI_PLUGIN_EXT_OP_OIDLIST     301
#define SLAPI_PLUGIN_EXT_OP_NAMELIST    302
#define SLAPI_PLUGIN_EXT_OP_BACKEND_FN 1948

/* preoperation plugin functions */
#define SLAPI_PLUGIN_PRE_BIND_FN     401
#define SLAPI_PLUGIN_PRE_UNBIND_FN   402
#define SLAPI_PLUGIN_PRE_SEARCH_FN   403
#define SLAPI_PLUGIN_PRE_COMPARE_FN  404
#define SLAPI_PLUGIN_PRE_MODIFY_FN   405
#define SLAPI_PLUGIN_PRE_MODRDN_FN   406
#define SLAPI_PLUGIN_PRE_ADD_FN      407
#define SLAPI_PLUGIN_PRE_DELETE_FN   408
#define SLAPI_PLUGIN_PRE_ABANDON_FN  409
#define SLAPI_PLUGIN_PRE_ENTRY_FN    410
#define SLAPI_PLUGIN_PRE_REFERRAL_FN 411
#define SLAPI_PLUGIN_PRE_RESULT_FN   412
#define SLAPI_PLUGIN_PRE_EXTOP_FN    413

/* internal preoperation plugin functions */
#define SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN    420
#define SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN 421
#define SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN 422
#define SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN 423
#define SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN   424

/* preoperation plugin to the backend */
#define SLAPI_PLUGIN_BE_PRE_ADD_FN    450
#define SLAPI_PLUGIN_BE_PRE_MODIFY_FN 451
#define SLAPI_PLUGIN_BE_PRE_MODRDN_FN 452
#define SLAPI_PLUGIN_BE_PRE_DELETE_FN 453
#define SLAPI_PLUGIN_BE_PRE_CLOSE_FN  454

/* preoperation plugin to the backend - just after transaction creation */
#define SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN              460
#define SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN           461
#define SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN           462
#define SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN           463
#define SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN 464

/* postoperation plugin functions */
#define SLAPI_PLUGIN_POST_BIND_FN        501
#define SLAPI_PLUGIN_POST_UNBIND_FN      502
#define SLAPI_PLUGIN_POST_SEARCH_FN      503
#define SLAPI_PLUGIN_POST_COMPARE_FN     504
#define SLAPI_PLUGIN_POST_MODIFY_FN      505
#define SLAPI_PLUGIN_POST_MODRDN_FN      506
#define SLAPI_PLUGIN_POST_ADD_FN         507
#define SLAPI_PLUGIN_POST_DELETE_FN      508
#define SLAPI_PLUGIN_POST_ABANDON_FN     509
#define SLAPI_PLUGIN_POST_ENTRY_FN       510
#define SLAPI_PLUGIN_POST_REFERRAL_FN    511
#define SLAPI_PLUGIN_POST_RESULT_FN      512
#define SLAPI_PLUGIN_POST_SEARCH_FAIL_FN 513
#define SLAPI_PLUGIN_POST_EXTOP_FN       514

/* internal preoperation plugin functions */
#define SLAPI_PLUGIN_INTERNAL_POST_ADD_FN    520
#define SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN 521
#define SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN 522
#define SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN 523

/* postoperation plugin to the backend */
#define SLAPI_PLUGIN_BE_POST_ADD_FN    550
#define SLAPI_PLUGIN_BE_POST_MODIFY_FN 551
#define SLAPI_PLUGIN_BE_POST_MODRDN_FN 552
#define SLAPI_PLUGIN_BE_POST_DELETE_FN 553
#define SLAPI_PLUGIN_BE_POST_OPEN_FN   554
#define SLAPI_PLUGIN_BE_POST_EXPORT_FN 556
#define SLAPI_PLUGIN_BE_POST_IMPORT_FN 557

/* postoperation plugin to the backend - just before transaction commit */
#define SLAPI_PLUGIN_BE_TXN_POST_ADD_FN    560
#define SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN 561
#define SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN 562
#define SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN 563

/* matching rule plugin functions */
#define SLAPI_PLUGIN_MR_FILTER_CREATE_FN  600
#define SLAPI_PLUGIN_MR_INDEXER_CREATE_FN 601
#define SLAPI_PLUGIN_MR_FILTER_MATCH_FN   602
#define SLAPI_PLUGIN_MR_FILTER_INDEX_FN   603
#define SLAPI_PLUGIN_MR_FILTER_RESET_FN   604
#define SLAPI_PLUGIN_MR_INDEX_FN          605
#define SLAPI_PLUGIN_MR_INDEX_SV_FN       606

/* matching rule plugin arguments */
#define SLAPI_PLUGIN_MR_OID                610
#define SLAPI_PLUGIN_MR_TYPE               611
#define SLAPI_PLUGIN_MR_VALUE              612
#define SLAPI_PLUGIN_MR_VALUES             613
#define SLAPI_PLUGIN_MR_KEYS               614
#define SLAPI_PLUGIN_MR_FILTER_REUSABLE    615
#define SLAPI_PLUGIN_MR_QUERY_OPERATOR     616
#define SLAPI_PLUGIN_MR_USAGE              617
/* new style matching rule syntax plugin functions */
#define SLAPI_PLUGIN_MR_FILTER_AVA         618
#define SLAPI_PLUGIN_MR_FILTER_SUB         619
#define SLAPI_PLUGIN_MR_VALUES2KEYS        620
#define SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA 621
#define SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB 622
#define SLAPI_PLUGIN_MR_FLAGS              623
#define SLAPI_PLUGIN_MR_NAMES              624
#define SLAPI_PLUGIN_MR_COMPARE            625
#define SLAPI_PLUGIN_MR_NORMALIZE          626

/* Defined values of SLAPI_PLUGIN_MR_QUERY_OPERATOR: */
#define SLAPI_OP_LESS              1
#define SLAPI_OP_LESS_OR_EQUAL     2
#define SLAPI_OP_EQUAL             3
#define SLAPI_OP_GREATER_OR_EQUAL  4
#define SLAPI_OP_GREATER           5
#define SLAPI_OP_SUBSTRING         6
#define SLAPI_OP_RANGE             0xff
#define SLAPI_OP_RANGE_NO_IDL_SORT 0x100
#define SLAPI_OP_RANGE_NO_ALLIDS   0x200

/* Defined values of SLAPI_PLUGIN_MR_USAGE: */
#define SLAPI_PLUGIN_MR_USAGE_INDEX 0
#define SLAPI_PLUGIN_MR_USAGE_SORT  1

/* Defined values for matchingRuleEntry accessor functions */
#define SLAPI_MATCHINGRULE_NAME     1
#define SLAPI_MATCHINGRULE_OID      2
#define SLAPI_MATCHINGRULE_DESC     3
#define SLAPI_MATCHINGRULE_SYNTAX   4
#define SLAPI_MATCHINGRULE_OBSOLETE 5

/* syntax plugin functions and arguments */
#define SLAPI_PLUGIN_SYNTAX_FILTER_AVA         700
#define SLAPI_PLUGIN_SYNTAX_FILTER_SUB         701
#define SLAPI_PLUGIN_SYNTAX_VALUES2KEYS        702
#define SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA 703
#define SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB 704
#define SLAPI_PLUGIN_SYNTAX_NAMES              705
#define SLAPI_PLUGIN_SYNTAX_OID                706
#define SLAPI_PLUGIN_SYNTAX_FLAGS              707
#define SLAPI_PLUGIN_SYNTAX_COMPARE            708

/* user defined substrlen; not stored in slapdplugin, but pblock itself */
#define SLAPI_SYNTAX_SUBSTRLENS               709
#define SLAPI_MR_SUBSTRLENS                   SLAPI_SYNTAX_SUBSTRLENS /* alias */
#define SLAPI_PLUGIN_SYNTAX_VALIDATE          710
#define SLAPI_PLUGIN_SYNTAX_NORMALIZE         711
#define SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED 712
#define SLAPI_PLUGIN_SYNTAX_FILTER_DATA       713

/* ACL plugin functions and arguments */
#define SLAPI_PLUGIN_ACL_INIT         730
#define SLAPI_PLUGIN_ACL_SYNTAX_CHECK 731
#define SLAPI_PLUGIN_ACL_ALLOW_ACCESS 732
#define SLAPI_PLUGIN_ACL_MODS_ALLOWED 733
#define SLAPI_PLUGIN_ACL_MODS_UPDATE  734


#define ACLPLUGIN_ACCESS_DEFAULT              0
#define ACLPLUGIN_ACCESS_READ_ON_ENTRY        1
#define ACLPLUGIN_ACCESS_READ_ON_ATTR         2
#define ACLPLUGIN_ACCESS_READ_ON_VLV          3
#define ACLPLUGIN_ACCESS_MODRDN               4
#define ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS 5

/* MMR plugin functions and arguments */
#define SLAPI_PLUGIN_MMR_BETXN_PREOP		761
#define SLAPI_PLUGIN_MMR_BETXN_POSTOP		762

/* Authorization types */
#define SLAPI_BE_MAXNESTLEVEL 742
#define SLAPI_CLIENT_DNS 745

/* Password storage scheme functions and arguments */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN 800
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN 801 /* only meaningfull for reversible encryption */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN 802

#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME     810 /* name of the method: SHA, SSHA... */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD 811 /* value sent over LDAP */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD   812 /* value from the DB */

/* entry fetch and entry store values */
#define SLAPI_PLUGIN_ENTRY_FETCH_FUNC 813
#define SLAPI_PLUGIN_ENTRY_STORE_FUNC 814
#define SLAPI_PLUGIN_ENABLED          815

#define SLAPI_PLUGIN_CONFIG_AREA      816
#define SLAPI_PLUGIN_CONFIG_DN        817

/*
 * Defined values of SLAPI_PLUGIN_SYNTAX_FLAGS:
 */
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORKEYS   1
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING 2

/* controls we know about */
#define SLAPI_MANAGEDSAIT 1000
#define SLAPI_PWPOLICY    1001

/* arguments that are common to all operation */
#define SLAPI_TARGET_SDN      47 /* target sdn of the operation */
#define SLAPI_TARGET_ADDRESS  48 /* target address (dn + uniqueid) should be normalized */
#define SLAPI_TARGET_UNIQUEID 49 /* target uniqueid of the operation */
#define SLAPI_TARGET_DN       50 /* DEPRECATED.  target dn of the operation. It actually points DN in SLAPI_TARGET_SDN. */
#define SLAPI_REQCONTROLS     51 /* request controls */

/* Copies of entry before and after add, mod, mod[r]dn operations */
#define SLAPI_ENTRY_PRE_OP  52
#define SLAPI_ENTRY_POST_OP 53
/* a PRE_ENTRY_FN may alter the entry to be returned to the client -
   SLAPI_SEARCH_ORIG_ENTRY holds the original entry from
   the database - this must not be changed
   SLAPI_SEARCH_ENTRY_COPY holds a copy of the original entry that
   has been modified by the plugin - this will be NULL by default -
   if a plugin needs to modify the entry, it should first check to
   see if there is already a SLAPI_SEARCH_ENTRY_COPY - if not, the
   plugin must use slapi_entry_dup() or similar to make a copy, edit
   the copy, then store it in SLAPI_SEARCH_ENTRY_COPY - the internal
   server code will free SLAPI_SEARCH_ENTRY_COPY
*/
#define SLAPI_SEARCH_ENTRY_ORIG SLAPI_ENTRY_PRE_OP
#define SLAPI_SEARCH_ENTRY_COPY SLAPI_ENTRY_POST_OP
/* for plugin init functions, this is the plugin config entry */
#define SLAPI_PLUGIN_CONFIG_ENTRY SLAPI_ENTRY_PRE_OP

/* LDAPv3 controls to be sent with the operation result */
#define SLAPI_RESCONTROLS    55
#define SLAPI_ADD_RESCONTROL 56 /* add result control */

/* Extra notes to be logged within access log RESULT lines */
#define SLAPI_OPERATION_NOTES        57

/* Remember these are FLAGS in a bitmask! */
typedef enum _slapi_op_note_t {
    SLAPI_OP_NOTE_UNINDEXED = 0x01,
    SLAPI_OP_NOTE_SIMPLEPAGED = 0x02,
    SLAPI_OP_NOTE_FULL_UNINDEXED = 0x04,
    SLAPI_OP_NOTE_FILTER_INVALID = 0x08,
} slapi_op_note_t;


/* Allows controls to be passed before operation object is created */
#define SLAPI_CONTROLS_ARG 58

/* specify whether pblock content should be destroyed when the pblock is destroyed */
#define SLAPI_DESTROY_CONTENT 59

/* add arguments */
#define SLAPI_ADD_TARGET                  SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_ADD_TARGET_SDN              SLAPI_TARGET_SDN
#define SLAPI_ADD_ENTRY                   60
#define SLAPI_ADD_EXISTING_DN_ENTRY       61
#define SLAPI_ADD_PARENT_ENTRY            62
#define SLAPI_ADD_PARENT_UNIQUEID         63
#define SLAPI_ADD_EXISTING_UNIQUEID_ENTRY 64

/* bind arguments */
#define SLAPI_BIND_TARGET        SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_BIND_TARGET_SDN    SLAPI_TARGET_SDN
#define SLAPI_BIND_METHOD        70
#define SLAPI_BIND_CREDENTIALS   71  /* v3 only */
#define SLAPI_BIND_SASLMECHANISM 72 /* v3 only */
/* bind return values */
#define SLAPI_BIND_RET_SASLCREDS 73 /* v3 only */

/* compare arguments */
#define SLAPI_COMPARE_TARGET     SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_COMPARE_TARGET_SDN SLAPI_TARGET_SDN
#define SLAPI_COMPARE_TYPE       80
#define SLAPI_COMPARE_VALUE      81

/* delete arguments */
#define SLAPI_DELETE_TARGET            SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_DELETE_TARGET_SDN        SLAPI_TARGET_SDN
#define SLAPI_DELETE_EXISTING_ENTRY    SLAPI_ADD_EXISTING_DN_ENTRY
#define SLAPI_DELETE_GLUE_PARENT_ENTRY SLAPI_ADD_PARENT_ENTRY
#define SLAPI_DELETE_BEPREOP_ENTRY     SLAPI_ENTRY_PRE_OP
#define SLAPI_DELETE_BEPOSTOP_ENTRY    SLAPI_ENTRY_POST_OP

/* modify arguments */
#define SLAPI_MODIFY_TARGET         SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_MODIFY_TARGET_SDN     SLAPI_TARGET_SDN
#define SLAPI_MODIFY_MODS           90
#define SLAPI_MODIFY_EXISTING_ENTRY SLAPI_ADD_EXISTING_DN_ENTRY

/* modrdn arguments */
#define SLAPI_MODRDN_TARGET              SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_MODRDN_TARGET_SDN          SLAPI_TARGET_SDN
#define SLAPI_MODRDN_NEWRDN              100
#define SLAPI_MODRDN_DELOLDRDN           101
#define SLAPI_MODRDN_NEWSUPERIOR         102 /* v3 only */
#define SLAPI_MODRDN_NEWSUPERIOR_SDN     103 /* v3 only */
#define SLAPI_MODRDN_EXISTING_ENTRY      SLAPI_ADD_EXISTING_DN_ENTRY
#define SLAPI_MODRDN_PARENT_ENTRY        104
#define SLAPI_MODRDN_NEWPARENT_ENTRY     105
#define SLAPI_MODRDN_TARGET_ENTRY        106
#define SLAPI_MODRDN_NEWSUPERIOR_ADDRESS 107

/*
 * unnormalized dn argument (useful for MOD, MODRDN and DEL operations to carry
 * the original non-escaped dn as introduced by the client application)
 */
#define SLAPI_ORIGINAL_TARGET_DN 109
#define SLAPI_ORIGINAL_TARGET    SLAPI_ORIGINAL_TARGET_DN

/* search arguments */
#define SLAPI_SEARCH_TARGET     SLAPI_TARGET_DN /* DEPRECATED */
#define SLAPI_SEARCH_TARGET_SDN SLAPI_TARGET_SDN
#define SLAPI_SEARCH_SCOPE      110
#define SLAPI_SEARCH_DEREF      111
#define SLAPI_SEARCH_SIZELIMIT  112
#define SLAPI_SEARCH_TIMELIMIT  113
#define SLAPI_SEARCH_FILTER     114
#define SLAPI_SEARCH_STRFILTER  115
#define SLAPI_SEARCH_ATTRS      116
#define SLAPI_SEARCH_GERATTRS  1160
#define SLAPI_SEARCH_REQATTRS  1161
#define SLAPI_SEARCH_ATTRSONLY  117
#define SLAPI_SEARCH_IS_AND     118
#define SLAPI_SEARCH_FILTER_INTENDED 119

/* abandon arguments */
#define SLAPI_ABANDON_MSGID 120

/* seq access arguments */
#define SLAPI_SEQ_TYPE 150
#define SLAPI_SEQ_ATTRNAME 151
#define SLAPI_SEQ_VAL 152

/* extended operation arguments */
#define SLAPI_EXT_OP_REQ_OID   160 /* v3 only */
#define SLAPI_EXT_OP_REQ_VALUE 161 /* v3 only */
/* extended operation return values */
#define SLAPI_EXT_OP_RET_OID   162 /* v3 only */
#define SLAPI_EXT_OP_RET_VALUE 163 /* v3 only */

/* extended filter arguments */
#define SLAPI_MR_FILTER_ENTRY   170 /* v3 only */
#define SLAPI_MR_FILTER_TYPE    171 /* v3 only */
#define SLAPI_MR_FILTER_VALUE   172 /* v3 only */
#define SLAPI_MR_FILTER_OID     173 /* v3 only */
#define SLAPI_MR_FILTER_DNATTRS 174 /* v3 only */

/* ldif2db arguments */
/* ldif file to convert to db */
#define SLAPI_LDIF2DB_FILE 180
/* check for duplicate values or not */
#define SLAPI_LDIF2DB_REMOVEDUPVALS 185
/* index only this attribute from existing database */
#define SLAPI_DB2INDEX_ATTRS 186
/* do not generate attribute indexes */
#define SLAPI_LDIF2DB_NOATTRINDEXES 187
/* list if DNs to include */
#define SLAPI_LDIF2DB_INCLUDE 188
/* list of DNs to exclude */
#define SLAPI_LDIF2DB_EXCLUDE 189
/* generate uniqueid */
#define SLAPI_LDIF2DB_GENERATE_UNIQUEID 175
#define SLAPI_LDIF2DB_NAMESPACEID       177
#define SLAPI_LDIF2DB_ENCRYPT           303
#define SLAPI_DB2LDIF_DECRYPT           304
/* uniqueid generation options */
#define SLAPI_UNIQUEID_GENERATE_NONE       0 /* do not generate */
#define SLAPI_UNIQUEID_GENERATE_TIME_BASED 1 /* generate time based id */
#define SLAPI_UNIQUEID_GENERATE_NAME_BASED 2 /* generate name based id */

/* db2ldif arguments */
/* print keys or not in ldif */
#define SLAPI_DB2LDIF_PRINTKEY 183
/* filename to export */
#define SLAPI_DB2LDIF_FILE 184
/* dump uniqueid */
#define SLAPI_DB2LDIF_DUMP_UNIQUEID  176
#define SLAPI_LDIF_CHANGELOG  1761
#define SLAPI_DB2LDIF_SERVER_RUNNING 197

/* db2ldif/ldif2db/bak2db/db2bak arguments */
#define SLAPI_BACKEND_INSTANCE_NAME 178
#define SLAPI_BACKEND_TASK          179
#define SLAPI_TASK_FLAGS            181

/* bulk import (online wire import) */
#define SLAPI_BULK_IMPORT_ENTRY 182
#define SLAPI_BULK_IMPORT_STATE 192
/* the actual states (these are not pblock args) */
#define SLAPI_BI_STATE_START 1
#define SLAPI_BI_STATE_DONE  2
#define SLAPI_BI_STATE_ADD   3
/* possible error codes from a bulk import */
#define SLAPI_BI_ERR_BUSY -23 /* backend is busy; try later */

/* transaction arguments */
#define SLAPI_PARENT_TXN                      190
#define SLAPI_TXN                             191
#define SLAPI_TXN_RUV_MODS_FN                1901
#define SLAPI_BACK_TRANSACTION_NOT_SUPPORTED 1902

/*
 * The following are used to pass information back and forth
 * between the front end and the back end.  The backend
 * creates a search result set as an opaque structure and
 * passes a reference to this back to the front end.  The
 * front end uses the backend's iterator entry point to
 * step through the results.  The entry, nentries, and
 * referrals options, below, are set/read by both the
 * front end and back end while stepping through the
 * search results.
 */
/* Search result set */
#define SLAPI_SEARCH_RESULT_SET 193
/* Estimated search result set size (for paged results) */
#define SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE 1930
/* Search result - next entry returned from search result set */
#define SLAPI_SEARCH_RESULT_ENTRY     194
#define SLAPI_SEARCH_RESULT_ENTRY_EXT 1944
/* Number of entries returned from search */
#define SLAPI_NENTRIES 195
/* Any referrals encountered during the search */
#define SLAPI_SEARCH_REFERRALS 196
/* for search operations, allows plugins to provide
   controls to pass for each entry or referral returned
   corresponds to pb_search_ctrls */
#define SLAPI_SEARCH_CTRLS 198

/* plugin be/betxn operations */
#define SLAPI_PLUGIN_ADD_OP    199
#define SLAPI_PLUGIN_MOD_OP    200
#define SLAPI_PLUGIN_MODRDN_OP 201
#define SLAPI_PLUGIN_DEL_OP    202

#define SLAPI_RESULT_CODE      881
#define SLAPI_RESULT_TEXT      882
#define SLAPI_RESULT_MATCHED   883

#define SLAPI_PB_RESULT_TEXT   885

/* Size of the database, in kilobytes */
#define SLAPI_DBSIZE 199

/* Simple paged results index */
#define SLAPI_PAGED_RESULTS_INDEX  1945
#define SLAPI_PAGED_RESULTS_COOKIE 1949

/* USN Plugin flag for tombstone entries */
#define SLAPI_USN_INCREMENT_FOR_TOMBSTONE 1950

/* ACI Target Check */
#define SLAPI_ACI_TARGET_CHECK 1946

/* dbverify */
#define SLAPI_DBVERIFY_DBDIR 1947

/* convenience macros for checking modify operation types */
#define SLAPI_IS_MOD_ADD(x)     (((x) & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD)
#define SLAPI_IS_MOD_DELETE(x)  (((x) & ~LDAP_MOD_BVALUES) == LDAP_MOD_DELETE)
#define SLAPI_IS_MOD_REPLACE(x) (((x) & ~LDAP_MOD_BVALUES) == LDAP_MOD_REPLACE)

/* regex.c */
typedef struct slapi_regex_handle Slapi_Regex;

/**
 * Compiles a regular expression pattern. A thin wrapper of pcre_compile.
 *
 * \param pat Pattern to be compiled.
 * \param error The error string is set if the compile fails.
 * \return This function returns a pointer to the regex handler which stores
 * the compiled pattern. NULL if the compile fails.
 * \warning The regex handler should be released by slapi_re_free().
 */
Slapi_Regex *slapi_re_comp(const char *pat, char **error);
/**
 * Matches a compiled regular expression pattern against a given string.
 * A thin wrapper of pcre_exec.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string to be checked against the compiled pattern.
 * \param time_up If the current time is larger than the value, this function
 * returns immediately.  (-1) means no time limit.
 * \return This function returns 0 if the string did not match.
 * \return This function returns 1 if the string matched.
 * \return This function returns other values if any error occurred.
 * \warning The regex handler should be released by slapi_re_free().
 */
int slapi_re_exec(Slapi_Regex *re_handle, const char *subject, time_t time_up) __attribute__((deprecated));
/**
 * Matches a compiled regular expression pattern against a given string.
 * A thin wrapper of pcre_exec.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string to be checked against the compiled pattern.
 * returns immediately.  (-1) means no time limit.
 * \return This function returns 0 if the string did not match.
 * \return This function returns 1 if the string matched.
 * \return This function returns other values if any error occurred.
 * \warning The regex handler should be released by slapi_re_free().
 */
int32_t slapi_re_exec_nt(Slapi_Regex *re_handle, const char *subject);
/**
 * Substitutes '&' or '\#' in the param src with the matched string.
 *
 * This is identical to slapi_re_exec, except with no timeout.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string checked against the compiled pattern.
 * \param src A given string which could contain the substitution symbols.
 * \param dst A pointer pointing to the memory which stores the output string.
 * \param dstlen Size of the memory dst.
 * \return This function returns 1 if the substitution was successful.
 * \return This function returns 0 if the substitution failed.
 * \warning The regex handler should be released by slapi_re_free().
 */
int slapi_re_subs(Slapi_Regex *re_handle, const char *subject, const char *src, char **dst, unsigned long dstlen);
/**
 * Substitutes '&' or '\#' in the param src with the matched string.  If the 'src' is a search filter
 * do not remove & if it is part of a compound filter.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string checked against the compiled pattern.
 * \param src A given string which could contain the substitution symbols.
 * \param dst A pointer pointing to the memory which stores the output string.
 * \param dstlen Size of the memory dst.
 * \param filter Set to 1 if the src is a ldap search filter
 * \return This function returns 1 if the substitution was successful.
 * \return This function returns 0 if the substitution failed.
 * \warning The regex handler should be released by slapi_re_free().
 */
int slapi_re_subs_ext(Slapi_Regex *re_handle, const char *subject, const char *src, char **dst, unsigned long dstlen, int filter);
/**
 * Releases the regex handler which was returned from slapi_re_comp.
 *
 * \param re_handle The regex handler to be released.
 * \return nothing
 */
void slapi_re_free(Slapi_Regex *re_handle);

/* wrap non-portable LDAP API functions */
void slapi_ldap_value_free(char **vals);
int slapi_ldap_count_values(char **vals);
int slapi_ldap_url_parse(const char *url, LDAPURLDesc **ludpp, int require_dn, int *secure);
const char *slapi_urlparse_err2string(int err);
int slapi_ldap_get_lderrno(LDAP *ld, char **m, char **s);
#ifndef LDIF_OPT_NOWRAP
#define LDIF_OPT_NOWRAP 0x01UL
#endif
#ifndef LDIF_OPT_VALUE_IS_URL
#define LDIF_OPT_VALUE_IS_URL 0x02UL
#endif
#ifndef LDIF_OPT_MINIMAL_ENCODING
#define LDIF_OPT_MINIMAL_ENCODING 0x04UL
#endif
void slapi_ldif_put_type_and_value_with_options(char **out, const char *t, const char *val, int vlen, unsigned long options);

/* ldif_read_record lineno argument type depends on openldap version */
#if LDAP_VENDOR_VERSION >= 20434 /* changed in 2.4.34 */
typedef unsigned long int ldif_record_lineno_t;
#else
typedef int ldif_record_lineno_t;
#endif

/*
 * UTF-8 routines (should these move into libnls?)
 */
/* number of bytes in character */
int ldap_utf8len(const char *);
/* find next character */
char *ldap_utf8next(char *);
/* find previous character */
char *ldap_utf8prev(char *);
/* find n-th character */
char *ldap_utf8nextn(char *s, int n);
/* find n-th previous character from "from" */
char *ldap_utf8prevn(char *s, char *from, int n);
/* copy one character */
int ldap_utf8copy(char *dst, const char *src);
/* total number of characters */
size_t ldap_utf8characters(const char *);
/* get one UCS-4 character, and move *src to the next character */
unsigned long ldap_utf8getcc(const char **src);
/* UTF-8 aware strtok_r() */
char *ldap_utf8strtok_r(char *src, const char *brk, char **next);

/* like isalnum(*s) in the C locale */
int ldap_utf8isalnum(char *s);
/* like isalpha(*s) in the C locale */
int ldap_utf8isalpha(char *s);
/* like isdigit(*s) in the C locale */
int ldap_utf8isdigit(char *s);
/* like isxdigit(*s) in the C locale */
int ldap_utf8isxdigit(char *s);
/* like isspace(*s) in the C locale */
int ldap_utf8isspace(char *s);

#define LDAP_UTF8LEN(s) ((0x80 & *(unsigned char *)(s)) ? ldap_utf8len(s) : 1)
#define LDAP_UTF8NEXT(s) ((0x80 & *(unsigned char *)(s)) ? ldap_utf8next(s) : (s) + 1)
#define LDAP_UTF8INC(s) ((0x80 & *(unsigned char *)(s)) ? s = ldap_utf8next(s) : ++s)

#define LDAP_UTF8PREV(s) ldap_utf8prev(s)
#define LDAP_UTF8DEC(s) (s = ldap_utf8prev(s))

#define LDAP_UTF8COPY(d, s) ((0x80 & *(unsigned char *)(s)) ? ldap_utf8copy(d, s) : ((*(d) = *(s)), 1))
#define LDAP_UTF8GETCC(s) ((0x80 & *(unsigned char *)(s)) ? ldap_utf8getcc(&s) : *s++)
#define LDAP_UTF8GETC(s) ((0x80 & *(unsigned char *)(s)) ? ldap_utf8getcc((const char **)&s) : *s++)

/* by default will allow dups */
char **slapi_str2charray(char *str, char *brkstr);
/*
 * extended version of str2charray lets you disallow
 * duplicate values into the array.
 */
char **slapi_str2charray_ext(char *str, char *brkstr, int allow_dups);

#ifndef LDAP_PORT_MAX
#define LDAP_PORT_MAX 65535 /* API extension */
#endif

#ifndef LDAP_ALL_USER_ATTRS
#ifdef LDAP_ALL_USER_ATTRIBUTES
#define LDAP_ALL_USER_ATTRS LDAP_ALL_USER_ATTRIBUTES
#else
#define LDAP_ALL_USER_ATTRS "*"
#endif
#endif

/*
 * As per rfc3673
 */

#define LDAP_ALL_OPERATIONAL_ATTRS "+"

#ifndef LDAP_SASL_EXTERNAL
#define LDAP_SASL_EXTERNAL "EXTERNAL" /* TLS/SSL extension */
#endif

#ifndef LBER_SOCKET
#ifdef LBER_SOCKET_T
#define LBER_SOCKET LBER_SOCKET_T
#else
#define LBER_SOCKET int
#endif
#endif

/* Used to make unhashed passwords available to plugins. */
#define PSEUDO_ATTR_UNHASHEDUSERPASSWORD "unhashed#user#password"

/* Unhashed password */
#define SLAPD_UNHASHED_PW_OFF   0
#define SLAPD_UNHASHED_PW_ON    1
#define SLAPD_UNHASHED_PW_NOLOG 2

/**
 * Set given "type: value" to the plugin default config entry
 * (cn=plugin default config,cn=config) unless the same "type: value" pair
 * already exists in the entry.
 *
 * \param type Attribute type to add to the default config entry
 * \param value Attribute value to add to the default config entry
 * \return \c 0 if the operation was successful
 * \return non-0 if the operation was not successful
 */
int slapi_set_plugin_default_config(const char *type, Slapi_Value *value);

/**
 * Get attribute values of given type from the plugin default config entry
 * (cn=plugin default config,cn=config).
 *
 * \param type Attribute type to get from the default config entry
 * \param valueset Valueset holding the attribute values
 * \return \c 0 if the operation was successful
 * \return non-0 if the operation was not successful
 * \warning Caller is responsible to free attrs by slapi_ch_array_free
 */
int slapi_get_plugin_default_config(char *type, Slapi_ValueSet **valueset);
int slapi_check_tpr_limits(Slapi_PBlock *pb, Slapi_Entry *bind_target_entry, int send_result);
int slapi_check_account_lock(Slapi_PBlock *pb, Slapi_Entry *bind_target_entry, int pwresponse_req, int check_password_policy, int send_result);

/* backend get/set info */
/**
 * Get backend info based upon cmd
 *
 * \param be Backend from which the information will be retrieved
 * \param cmd macro to specify the information type
 * \param info pointer to store the information
 * \return \c 0 if the operation was successful
 * \return non-0 if the operation was not successful
 *
 * \note Implemented cmd:
 * BACK_INFO_DBENV - Get the dbenv
 * BACK_INFO_DBENV_CLBD - Get the changelog db for the backend
 * BACK_INFO_DBENV_OPENFLAGS - Get the dbenv openflags
 * BACK_INFO_INDEXPAGESIZE - Get the index page size
 */
int slapi_back_get_info(Slapi_Backend *be, int cmd, void **info);

/**
 * Set info to backend based upon cmd
 *
 * \param be Backend to which the infomation will be set
 * \param cmd macro to specify the information type
 * \param info pointer to the information
 * \return \c 0 if the operation was successful
 * \return non-0 if the operation was not successful
 * \warning No cmd is defined yet.
 */
int slapi_back_set_info(Slapi_Backend *be, int cmd, void *info);

/**
 * Execute cmd in backend
 *
 * \param be Backend where the command is executed
 * \param cmd macro to specify the execution type
 * \param info pointer to the information
 * \return \c 0 if the operation was successful
 * \return non-0 if the operation was not successful
 *
 * \note Implemented cmd:
 * BACK_INFO_CRYPT_INIT - Initialize cipher (info: back_info_crypt_init)
 * BACK_INFO_CRYPT_DESTROY - Free allocated during init data (info: back_info_crypt_destroy)
 * BACK_INFO_CRYPT_ENCRYPT_VALUE - Encrypt the given value (info: back_info_crypt_value)
 * BACK_INFO_CRYPT_DECRYPT_VALUE - Decrypt the given value (info: back_info_crypt_value)
 */
int slapi_back_ctrl_info(Slapi_Backend *be, int cmd, void *info);

/* cmd */
enum
{
    BACK_INFO_DBENV,               /* Get the dbenv */
    BACK_INFO_DBENV_CLDB,          /* Get the changelog */
    BACK_INFO_DBENV_CLDB_REMOVE,   /* Remove the changelog */
    BACK_INFO_DBENV_CLDB_RESET,    /* Recreate the changelog */
    BACK_INFO_DBENV_CLDB_UPGRADE,  /* Move an old cl file to the instance database */
    BACK_INFO_CLDB_SET_CONFIG,     /* Set the CL configuration for a backend database */
    BACK_INFO_CLDB_GET_CONFIG,     /* Get the CL configuration for a backend database */
    BACK_INFO_DB_PAGESIZE,         /* Get the db page size */
    BACK_INFO_INDEXPAGESIZE,       /* Get the index page size */
    BACK_INFO_DBENV_OPENFLAGS,     /* Get the dbenv openflags */
    BACK_INFO_CRYPT_INIT,          /* Ctrl: clcrypt_init */
    BACK_INFO_CRYPT_DESTROY,       /* Ctrl: clcrypt_destroy */
    BACK_INFO_CRYPT_ENCRYPT_VALUE, /* Ctrl: clcrypt_encrypt_value */
    BACK_INFO_CRYPT_DECRYPT_VALUE, /* Ctrl: clcrypt_decrypt_value */
    BACK_INFO_DIRECTORY,           /* Get the db directory path */
    BACK_INFO_INSTANCE_DIR,        /* Get the path to an instance */
    BACK_INFO_LOG_DIRECTORY,       /* Get the txn log directory */
    BACK_INFO_INDEX_KEY,           /* Get the status of a key in an index */
    BACK_INFO_DB_DIRECTORY,        /* Get the db directory */
    BACK_INFO_DBHOME_DIRECTORY,    /* Get the dbhome directory */
    BACK_INFO_IS_ENTRYRDN,         /* Get the flag for entryrdn */
    BACK_INFO_CLDB_FILENAME        /* Get the backend replication changelog name */
};

struct _back_info_index_key
{
    char *index;              /* input: name of the index (parentid) */
    char *key;                /* input: searched key (0) with equality -> '=0' */
    PRBool key_found;         /* output: TRUE if '=0' is found in the index */
    u_int32_t id;             /* output: if key_found it is the first value (suffix entryID) */
};
struct _back_info_crypt_init
{
    char *dn;                  /* input -- entry to store nsSymmetricKey */
    char *encryptionAlgorithm; /* input -- encryption algorithm */
    Slapi_Backend *be;         /* input -- backend to use */
    void *state_priv;          /* outout */
};
typedef struct _back_info_crypt_init back_info_crypt_init;

struct _back_info_crypt_destroy
{
    void *state_priv;          /* a structure to free */
};
typedef struct _back_info_crypt_destroy back_info_crypt_destroy;

struct _back_info_crypt_value
{
    void *state_priv;   /* input */
    struct berval *in;  /* input */
    struct berval *out; /* output */
};
typedef struct _back_info_crypt_value back_info_crypt_value;

struct _back_info_config_entry
{
    char *dn;           /* input  -- part of dn below backend config entry */
    Slapi_Entry *ce;    /* output -- requested config entry */
};
typedef struct _back_info_config_entry back_info_config_entry;

#define BACK_CRYPT_OUTBUFF_EXTLEN 16

/**
 * Convert unsigned char (8 bit) value to a hex string.  Writes to the string.
 * The caller must ensure enough space to write 2 bytes.  If the upper parameter
 * is TRUE, will use upper case A-F instead of lower case a-f for hex numbers.
 * Returns the address after the last byte written to encourage usage like this:
 * \code
 * char *ptr = slapi_u_to_hex(val, buf, 0);
 * ptr = slapi_u_to_hex(val2, ptr, 0);
 * ...
 * ptr = slapi_u_to_hex(valN, ptr, 0);
 * *ptr = 0;
 * \endcode
 *
 * \param val unsigned value to convert to string
 * \param s string to write hex value into
 * \param upper if TRUE use A-F otherwise use a-f
 * \return address of next char after writing value to s
 *
 * \note Does not null terminate s - caller is responsible for that
 */
char *slapi_u8_to_hex(uint8_t val, char *s, uint8_t upper);

/**
 * Convert unsigned short (16 bit) value to a hex string.  Writes to the string.
 * The caller must ensure enough space to write 4 bytes.  If the upper parameter
 * is TRUE, will use upper case A-F instead of lower case a-f for hex numbers.
 * Returns the address after the last byte written to encourage usage like this:
 * \code
 * char *ptr = slapi_u_to_hex(val, buf, 0);
 * ptr = slapi_u_to_hex(val2, ptr, 0);
 * ...
 * ptr = slapi_u_to_hex(valN, ptr, 0);
 * *ptr = 0;
 * \endcode
 *
 * \param val unsigned value to convert to string
 * \param s string to write hex value into
 * \param upper if TRUE use A-F otherwise use a-f
 * \return address of next char after writing value to s
 *
 * \note Does not null terminate s - caller is responsible for that
 */
char *slapi_u16_to_hex(uint16_t val, char *s, uint8_t upper);

/**
 * Convert unsigned int (32 bit) value to a hex string.  Writes to the string.
 * The caller must ensure enough space to write 4 bytes.  If the upper parameter
 * is TRUE, will use upper case A-F instead of lower case a-f for hex numbers.
 * Returns the address after the last byte written to encourage usage like this:
 * \code
 * char *ptr = slapi_u_to_hex(val, buf, 0);
 * ptr = slapi_u_to_hex(val2, ptr, 0);
 * ...
 * ptr = slapi_u_to_hex(valN, ptr, 0);
 * *ptr = 0;
 * \endcode
 *
 * \param val unsigned value to convert to string
 * \param s string to write hex value into
 * \param upper if TRUE use A-F otherwise use a-f
 * \return address of next char after writing value to s
 *
 * \note Does not null terminate s - caller is responsible for that
 */
char *slapi_u32_to_hex(uint32_t val, char *s, uint8_t upper);

/**
 * Convert unsigned long long (64 bit) value to a hex string.  Writes to the string.
 * The caller must ensure enough space to write 4 bytes.  If the upper parameter
 * is TRUE, will use upper case A-F instead of lower case a-f for hex numbers.
 * Returns the address after the last byte written to encourage usage like this:
 * \code
 * char *ptr = slapi_u_to_hex(val, buf, 0);
 * ptr = slapi_u_to_hex(val2, ptr, 0);
 * ...
 * ptr = slapi_u_to_hex(valN, ptr, 0);
 * *ptr = 0;
 * \endcode
 *
 * \param val unsigned value to convert to string
 * \param s string to write hex value into
 * \param upper if TRUE use A-F otherwise use a-f
 * \return address of next char after writing value to s
 *
 * \note Does not null terminate s - caller is responsible for that
 */
char *slapi_u64_to_hex(uint64_t val, char *s, uint8_t upper);

/**
 * Convert a char to its integral hex value e.g. '0' -> 0 or 'a' -> 10.
 * This only works on one caller at a time.  If you want to convert a string
 * of decimal/hex numbers to its integral value, see slapi_str_to_u8 et. al.
 * This uses a lookup table so it should be extremely fast.
 *
 * \param c character value to convert
 * \return integral value of the given char or -1 if not a valid decimal/hex digit
 */
int slapi_hexchar2int(char c);

/**
 * Convert a string of 2 decimal/hex characters to a 1 byte (8-bit) unsigned value.
 * This function does no checking - it assumes s is non-NULL and well-formed.
 *
 * \param s convert the first 2 chars of this decimal/hex char string to its integral value
 * \return the integral value
 */
uint8_t slapi_str_to_u8(const char *s);

/**
 * Convert a string of 4 decimal/hex characters to a 2 byte (16-bit) unsigned value.
 * This function does no checking - it assumes s is non-NULL and well-formed.
 *
 * \param s convert the first 4 chars of this decimal/hex char string to its integral value
 * \return the integral value
 */
uint16_t slapi_str_to_u16(const char *s);

/**
 * Convert a string of 8 decimal/hex characters to a 4 byte (32-bit) unsigned value.
 * This function does no checking - it assumes s is non-NULL and well-formed.
 *
 * \param s convert the first 8 chars of this decimal/hex char string to its integral value
 * \return the integral value
 */
uint32_t slapi_str_to_u32(const char *s);

/**
 * Convert a string of 16 decimal/hex characters to a 8 byte (64-bit) unsigned value.
 * This function does no checking - it assumes s is non-NULL and well-formed.
 *
 * \param s convert the first 16 chars of this decimal/hex char string to its integral value
 * \return the integral value
 */
uint64_t slapi_str_to_u64(const char *s);

void slapi_set_plugin_open_rootdn_bind(Slapi_PBlock *pb);

PRBool slapi_disorderly_shutdown(PRBool set);

/*
 * Public entry extension getter/setter functions
 *
 * Currently, only slapi_pw_get/set_entry_ext is implemented.
 * The functions are in pw.c.  Detailed usage of the factory
 * is found in the comments at the top of factory.c.
 *
 * When you plan to add other entry extension code AND
 * the type-value pair is managed via ordinary mod,
 * setter, getter and copy function having the same API
 * are supposed to be implemented, then add the set to
 * attrs_in_extension list in entry.c.  The set is called
 * in slapi_entry_apply_mod_extension.
 *
 * Note: setter and getter are public, but copy function
 * is not. (for the copy function, see pw_copy_entry_ext in pw.c)
 */
/* operation used in the entry extension setter */
#define SLAPI_EXT_SET_ADD     0
#define SLAPI_EXT_SET_REPLACE 1

/**
 * Get entry extension
 *
 * \param entry is the entry to retrieve the extension from
 * \param vals is the array of (Slapi_Value *), which directly refers the extension.  Caller must duplicate it to use it for other than referring.
 *
 * \return LDAP_SUCCESS if successful.
 * \return non-zero otherwise.
 */
int slapi_pw_get_entry_ext(Slapi_Entry *entry, Slapi_Value ***vals);

/**
 * Set entry extension
 *
 * \param entry is the entry to set the extension to
 * \param vals is the array of (Slapi_Value *), which is consumed in slapi_pw_set_ext if the call is successful.
 * \param flags: SLAPI_EXT_SET_ADD -- add vals to the existing extension if any.
 *               SLAPI_EXT_SET_REPLACE -- replace vals with the existing extension if any.
 * No difference if there is no extension in the entry.
 *
 * \return LDAP_SUCCESS if successful.
 * \return non-zero otherwise.
 */
int slapi_pw_set_entry_ext(Slapi_Entry *entry, Slapi_Value **vals, int flags);

/**
 * Get stashed clear password.
 * If multiple of them are in the extension, the first one is returned.
 *
 * \param entry is the entry to retrieve the extension from
 *
 * \return a pointer to the clear password string.  Caller is responsible to free the string.
 */
char *slapi_get_first_clear_text_pw(Slapi_Entry *entry);

/**
 * Return the string equivalent of an NSPR error
 *  *
 * \param a NSPR error code
 *
 * \return a pointer to the error code string.
 */
char *slapi_pr_strerror(const PRErrorCode prerrno);

/**
 * Return the string equivalent of an OS error
 *
 * \param a OS error code
 *
 * \return a pointer to the system error code string.
 */
const char *slapi_system_strerror(const int syserrno);

/* event queue routines and data types */

/**
 * Represents a scheduled event object.
 */
typedef void *Slapi_Eq_Context;

/**
 * Represents the function that will be performed when an event is triggered.
 */
typedef void (*slapi_eq_fn_t)(time_t when, void *arg);

/**
 * Cause an event to happen exactly once.
 *
 * \param fn The function to call when the event is triggered.
 * \param arg An argument to pass to the called function.
 * \param when The time that the function should be called(MONOTONIC clock).
 *
 * \return slapi_eq_context
 */
Slapi_Eq_Context slapi_eq_once_rel(slapi_eq_fn_t fn, void *arg, time_t when);

/**
 * Cause an event to happen repeatedly.
 *
 * \param fn The function to call when the vent is triggered.
 * \param arg An argument to pass to the called function.
 * \param when The time that the function should be called(MONOTONIC clock).
 * \param interval The amount of time (in milliseconds) between
 *                 successive calls to the function.
 *
 * \return slapi_eq_context
 */
Slapi_Eq_Context slapi_eq_repeat_rel(slapi_eq_fn_t fn, void *arg, time_t when, unsigned long interval);

/**
 * Cause a scheduled event to be canceled.
 *
 * \param ctx The event object to cancel
 *
 * \return 1 If event was found and canceled.
 * \return 0 If event was not found in the queue.
 */
int slapi_eq_cancel_rel(Slapi_Eq_Context ctx);

/**
 * Return the event's argument.
 *
 * \param ctx The event object
 *
 * \return A pointer to the event argument.
 */
void *slapi_eq_get_arg_rel(Slapi_Eq_Context ctx);

/*
 * These event queue functions are now DEPRECATED as they REALTIME clocks
 * instead of the preferred MONOTONIC clocks.
 */

/**
 * Cause an event to happen exactly once.
 *
 * \param fn The function to call when the event is triggered.
 * \param arg An argument to pass to the called function.
 * \param when The time that the function should be called(REALTIME clock).
 *
 * \return slapi_eq_context
 */
Slapi_Eq_Context slapi_eq_once(slapi_eq_fn_t fn, void *arg, time_t when) __attribute__((deprecated));

/**
 * Cause an event to happen repeatedly.
 *
 * \param fn The function to call when the vent is triggered.
 * \param arg An argument to pass to the called function.
 * \param when The time that the function should be called(REALTIME clock).
 * \param interval The amount of time (in milliseconds) between
 *                 successive calls to the function.
 *
 * \return slapi_eq_context
 */
Slapi_Eq_Context slapi_eq_repeat(slapi_eq_fn_t fn, void *arg, time_t when, unsigned long interval) __attribute__((deprecated));

/**
 * Cause a scheduled event to be canceled.
 *
 * \param ctx The event object to cancel
 *
 * \return 1 If event was found and canceled.
 * \return 0 If event was not found in the queue.
 */
int slapi_eq_cancel(Slapi_Eq_Context ctx) __attribute__((deprecated));

/**
 * Return the event's argument.
 *
 * \param ctx The event object
 *
 * \return A pointer to the event argument.
 */
void *slapi_eq_get_arg(Slapi_Eq_Context ctx) __attribute__((deprecated));

/**
 * Construct a full path and name of a plugin.
 *
 * \param dir The Directory where the plugin is located.
 * \param name The name of the plugin.
 *
 * \return absolute path of the plugin.  Caller is responsible to free it.
 */
char *slapi_get_plugin_name(const char *dir, const char *name);

/**
 * Takes a encoded password and return the storage scheme.
 * Return the password scheme for value "passwd_val".
 *
 * If "valpwdp" is not NULL, it is set to point to the value with any
 * prefix removed.
 *
 * If no matching scheme is found and first_is_default is non-zero, the
 * first scheme is returned.  If no matching scheme is found and
 * first_is_default is zero, NULL is returned.
 *
 * \param passwd_val The password value.
 * \param valpwdp If the char pointer passed is is not NULL, this gets set to the password value
 * without the password storage scheme prefix.
 * \param first_is_default  Return the first scheme, if there are no other matches.
 *
 * \return password storage scheme
 */
PWScheme *slapi_pw_val2scheme(char *passwd_val, char **valpwdp, int first_is_default);

/**
 * Compare a clear text password to an encoded password value.
 *
 * \param pass_scheme The password scheme.
 * \param clear_pw The clear text password.
 * \param encoded_pw The encoded password value, minus the storage scheme prefix.
 *
 * \return 0 If the passwords match.
 * \return 1 if passwords do not match.
 */
int slapi_pw_cmp(PWScheme *pass_scheme, char *clear_pw, char *encoded_pw);

/**
 * Get the password storage scheme name
 *
 * \param pass_scheme The password storage scheme object.
 *
 * \return The storage scheme name.
 */
char *slapi_pw_get_scheme_name(PWScheme *pass_scheme);

/**
 * Free a PWScheme object.
 *
 * \param pass_scheme PWScheme object.
 *
 * \return Nothing.
 */
void slapi_free_pw_scheme(PWScheme *pass_scheme);

/**
 * Check if the plugin is still running/started.
 *
 * \param Slapi_PBlock object.
 *
 * \return 0 If plugin is not running.
 * \return 1 If plugin is running.
 */
int slapi_plugin_running(Slapi_PBlock *pb);

/**
 * For slapi registered tasks that create their own threads.  Mark that the task
 * thread has started.  This arg must be passed to the thread function.
 *
 * \param void *arg This is the function argument passed to the task function.
 *
 * \return Nothing
 */
void slapi_plugin_op_started(void *arg);

/**
 * For slapi registered tasks that create their own threads.  Mark that the task
 * thread has finished.  This arg must be passed to the thread function.
 *
 * \param void *arg This is the function argument passed to the task function.
 *
 * \return Nothing
 */
void slapi_plugin_op_finished(void *arg);

/** Check if rdn is a slecial rdn/dn or not.
 *
 * \param rdn rdn/dn to check
 * \param flags specify the type: RDN_IS_TOMBSTONE or RDN_IS_CONFLICT or 0
 *
 * \return 1 if rdn matches the flag.
 *   If flag is IS_TOMBSTONE, returns 1 if rdn is a tombstone rdn/dn.
 *   If flag is IS_CONFLICT, returns 1 if rdn is a conflict rdn/dn.
 * \return 0 otherwise
 */
#define RDN_IS_TOMBSTONE 0x1
#define RDN_IS_CONFLICT  0x2
int slapi_is_special_rdn(const char *rdn, int flag);

/**
 * Sleeps for PRIntervalTime ticks defined in NSPR library
 *
 * \param PRIntervalTime ticks
 *
 * \return Nothing
 */
void DS_Sleep(PRIntervalTime ticks);


#ifdef HAVE_CLOCK_GETTIME
/**
 * Diffs two timespects a - b into *diff. This is useful with
 * clock_monotonic to find time taken to perform operations.
 *
 * \param struct timespec a the "end" time.
 * \param struct timespec b the "start" time.
 * \param struct timespec c the difference.
 */
void slapi_timespec_diff(struct timespec *a, struct timespec *b, struct timespec *diff);
/**
 * Given an operation, determine the time elapsed since the op
 * began.
 *
 * \param Slapi_Operation o - the operation which is inprogress
 * \param struct timespec *elapsed - location where the time difference will be
 * placed.
 */
void slapi_operation_time_elapsed(Slapi_Operation *o, struct timespec *elapsed);
/**
 * Given an operation, see it's initiated time based on clock_monotonic
 * this is useful for timeout parameters and checks.
 *
 * \param Slapi_Operation o - the operation that is in progress
 * \param struct timespec *initiated - location where we will populate the init time.
 */
void slapi_operation_time_initiated(Slapi_Operation *o, struct timespec *initiated);
/**
 * Given an operation, determine the time elapsed since the op
 * was actually started.
 *
 * \param Slapi_Operation o - the operation which is inprogress
 * \param struct timespec *elapsed - location where the time difference will be
 * placed.
 */
void slapi_operation_op_time_elapsed(Slapi_Operation *o, struct timespec *elapsed);
/**
 * Given an operation, determine the time elapsed that the op spent
 * in the work queue before actually being dispatched to a worker thread
 *
 * \param Slapi_Operation o - the operation which is inprogress
 * \param struct timespec *elapsed - location where the time difference will be
 * placed.
 */
void slapi_operation_workq_time_elapsed(Slapi_Operation *o, struct timespec *elapsed);
/**
 * Set the time the operation actually started
 *
 * \param Slapi_Operation o - the operation which is inprogress
 */
void slapi_operation_set_time_started(Slapi_Operation *o);
#endif

/**
 * Store a 32bit integral value atomicly
 *
 * \param ptr - integral pointer
 * \param val - pointer to integral value (use integral type int32_t with ATOMIC_INT, or uint64_t
 * with ATOMIC_LONG & ATOMIC_GENERIC)
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 */
void slapi_atomic_store_32(int32_t *ptr, int32_t val, int memorder);

/**
 * Store a 64bit integral value atomicly
 *
 * \param ptr - integral pointer
 * \param val - pointer to integral value (use integral type int32_t with ATOMIC_INT, or uint64_t
 * with ATOMIC_LONG & ATOMIC_GENERIC)
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 */
void slapi_atomic_store_64(uint64_t *ptr, uint64_t val, int memorder);

/**
 * Get a 32bit integral value atomicly
 *
 * \param ptr - integral pointer
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return -
 */
int32_t slapi_atomic_load_32(int32_t *ptr, int memorder);

/**
 * Get a 64bit integral value atomicly
 *
 * \param ptr - integral pointer
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return ptr value
 */
uint64_t slapi_atomic_load_64(uint64_t *ptr, int memorder);

/**
 * Increment a 32bit integral atomicly
 *
 * \param ptr - pointer to integral to increment
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return - new value of ptr
 */
int32_t slapi_atomic_incr_32(int32_t *ptr, int memorder);

/**
 * Increment a 64bitintegral atomicly
 *
 * \param ptr - pointer to integral to increment
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return - new value of ptr
 */
uint64_t slapi_atomic_incr_64(uint64_t *ptr, int memorder);

/**
 * Decrement a 32bit integral atomicly
 *
 * \param ptr - pointer to integral to decrement
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return - new value of ptr
 */
int32_t slapi_atomic_decr_32(int32_t *ptr, int memorder);

/**
 * Decrement a 64bitintegral atomicly
 *
 * \param ptr - pointer to integral to decrement
 * \param memorder - __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE,
 * __ATOMIC_RELEASE, __ATOMIC_ACQ_REL, __ATOMIC_SEQ_CST
 * \return - new value of ptr
 */
uint64_t slapi_atomic_decr_64(uint64_t *ptr, int memorder);

/* helper function */
const char * slapi_fetch_attr(Slapi_Entry *e, char *attrname, char *default_val);

/**
 * Get a Slapi_Entry via an internal search.  The caller then needs to call
 * slapi_get_entry_done() to free any resources allocated to get the entry
 *
 * \param pb - slapi_pblock pointer (the function will allocate if necessary)
 * \param dn - Slapi_DN of the entry to retrieve
 * \param attrs - char list of attributes to get
 * \param ret_entry - pointer to a Slapi_entry wer the returned entry is stored
 * \param component_identity - plugin component
 *
 * \return - ldap result code
 */
int32_t slapi_search_get_entry(Slapi_PBlock **pb, Slapi_DN *dn, char **attrs, Slapi_Entry **ret_entry, void *component_identity);

/**
 * Free the resources allocated by slapi_search_get_entry()
 *
 * \param pb - slapi_pblock pointer
 */
void slapi_search_get_entry_done(Slapi_PBlock **pb);

#ifdef __cplusplus
}
#endif

#endif /* SLAPIPLUGIN_H_ */

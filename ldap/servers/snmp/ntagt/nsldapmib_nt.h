/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*-------------------------------------------------------------------------
 *
 * nsldapmib_nt.h - Definitions for NS Directory Server's MIB on NT.
 * 
 * Revision History:
 * 07/25/1997		Steve Ross	Created
 *
 *
 *
 *-----------------------------------------------------------------------*/

#ifndef __NSLDAPMIB_NT_H_
#define __NSLDAPMIB_NT_H_

/*-------------------------------------------------------------------------
 *
 * Defines 
 *
 *-----------------------------------------------------------------------*/

#define MAGT_MIB_PREFIX_LEN MIB_OidPrefix.idLength
#define MAGT_MAX_STRING_LEN 255

#define MAGT_MIB_ACCESS_READ      0
#define MAGT_MIB_ACCESS_WRITE     1
#define MAGT_MIB_ACCESS_READWRITE 2

#define MAGT_MIB_ACTION_GET     ASN_RFC1157_GETREQUEST
#define MAGT_MIB_ACTION_SET     ASN_RFC1157_SETREQUEST
#define MAGT_MIB_ACTION_GETNEXT ASN_RFC1157_GETNEXTREQUEST

/*
 * Macro to determine number of sub-oids in array.
 */
#define MAGT_OID_SIZEOF(Oid) (sizeof Oid / sizeof(UINT))

/*
 * Unique ID for each entry in the MIB.
 */
enum
{
  MAGT_ID_DESC = 0,
  MAGT_ID_VERS,
  MAGT_ID_ORG,
  MAGT_ID_LOC,
  MAGT_ID_CONTACT,
  MAGT_ID_NAME,
  /* operations table attrs */
  MAGT_ID_ANONYMOUS_BINDS,
  MAGT_ID_UNAUTH_BINDS,
  MAGT_ID_SIMPLE_AUTH_BINDS,  
  MAGT_ID_STRONG_AUTH_BINDS ,
  MAGT_ID_BIND_SECURITY_ERRORS,
  MAGT_ID_IN_OPS,
  MAGT_ID_READ_OPS,
  MAGT_ID_COMPARE_OPS,
  MAGT_ID_ADD_ENTRY_OPS,
  MAGT_ID_REMOVE_ENTRY_OPS,
  MAGT_ID_MODIFY_ENTRY_OPS,
  MAGT_ID_MODIFY_RDN_OPS,
  MAGT_ID_LIST_OPS,
  MAGT_ID_SEARCH_OPS,
  MAGT_ID_ONE_LEVEL_SEARCH_OPS,
  MAGT_ID_WHOLE_SUBTREE_SEARCH_OPS,
  MAGT_ID_REFERRALS,
  MAGT_ID_CHAININGS,
  MAGT_ID_SECURITY_ERRORS,
  MAGT_ID_ERRORS,
  /* entries table attrs */
  MAGT_ID_MASTER_ENTRIES,
  MAGT_ID_COPY_ENTRIES,
  MAGT_ID_CACHE_ENTRIES,
  MAGT_ID_CACHE_HITS,
  MAGT_ID_SLAVE_HITS,
  /* interaction table entries */
  MAGT_ID_DS_NAME,
  MAGT_ID_TIME_OF_CREATION,
  MAGT_ID_TIME_OF_LAST_ATTEMPT,
  MAGT_ID_TIME_OF_LAST_SUCCESS,
  MAGT_ID_FAILURES_SINCE_LAST_SUCCESS,
  MAGT_ID_FAILURES,
  MAGT_ID_SUCCESSES,
  MAGT_ID_URL,
  /* applIndex */
  MAGT_ID_APPLINDEX
};

/*-------------------------------------------------------------------------
 *
 * Types
 *
 *-----------------------------------------------------------------------*/
 
typedef struct MagtMibEntry
{
  AsnObjectIdentifier Oid;
  void *Storage;
  BYTE Type;
  UINT Access;
  UINT (*MibFunc)(UINT, struct MagtMibEntry *, RFC1157VarBind *);
  struct MagtMibEntry *MibNext;
  UINT uId;
} MagtMibEntry_t;

#include "nsldapagt_nt.h"
/*-------------------------------------------------------------------------
 *
 * Prototypes
 *
 *-----------------------------------------------------------------------*/

int MagtFillTrapVars(int                trapType, 
	  			     RFC1157VarBind   * trapVars, 
				     MagtStaticInfo_t * pCfgInfo);

UINT MagtMIBLeafFunc(IN UINT Action,
                     IN MagtMibEntry_t *MibPtr,
                     IN RFC1157VarBind *VarBind);

UINT MagtResolveVarBind(IN OUT RFC1157VarBind *VarBind,
                        IN UINT PduAction);

int Mib_init(MagtMibEntry_t **Mib, int ApplIndex);
#endif					/* __NSLDAPMIB_NT_H_ */

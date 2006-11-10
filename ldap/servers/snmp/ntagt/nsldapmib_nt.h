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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


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

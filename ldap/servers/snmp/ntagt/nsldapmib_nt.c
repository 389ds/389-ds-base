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
 * nsldapmib_nt.c - NS Directory Server's MIB for extended SNMP agent
 *                  on NT.
 * 
 * Revision History:
 * 07/25/1997	Steve Ross	Created
 *
 * 
 *-----------------------------------------------------------------------*/

#include <stdio.h>
#include <malloc.h>
#include <time.h>
#include <windows.h>
#include <malloc.h>
#include <snmp.h>
#include <mgmtapi.h>
#include "nsldapmib_nt.h"
#include "nsldapagt_nt.h"
#include "agtmmap.h"

/*-------------------------------------------------------------------------
 *
 * Globals
 *
 *-----------------------------------------------------------------------*/

/*
 * For ldap, the prefix to all MIB variables is 1.3.6.1.4.1.1450.7
 */
UINT OID_Prefix[] = {1, 3, 6, 1, 4, 1, 1450, 7};
AsnObjectIdentifier MIB_OidPrefix = {MAGT_OID_SIZEOF(OID_Prefix), 
                                     OID_Prefix};

/*
 * OID of each MIB variable.
 * For example, the OID for mtaReceivedMessages is:
 *  1.3.6.1.4.1.1450.7
 *  
 *  - nsldap        = netscape 7
 *  -dsOpsTable     = nsldap 1
 *  -dsEntriesTable = nsldap 2
 *	-dsIntTable     = nsldap 3
 *	-dsEntityINfo   = nsldap 5
 *    
 */


UINT OID_ApplIndex[]		     = {389};
/* setup the parts of the OID we know in advance */
          
/* ops table */
UINT OID_AnonymousBinds[]		 = {1, 1,  1};
UINT OID_UnAuthBinds[]			 = {1, 1,  2};
UINT OID_SimpleAuthBinds[]		 = {1, 1,  3};
UINT OID_StrongAuthBinds[]		 = {1, 1,  4};
UINT OID_BindSecurityErrors[]	 = {1, 1,  5};
UINT OID_InOps[]			     = {1, 1,  6};
UINT OID_ReadOps[]			     = {1, 1,  7};
UINT OID_CompareOps[]			 = {1, 1,  8};
UINT OID_AddEntryOps[]			 = {1, 1,  9};
UINT OID_RemoveEntryOps[]		 = {1, 1, 10};
UINT OID_ModifyEntryOps[]		 = {1, 1, 11};
UINT OID_ModifyRDNOps[]			 = {1, 1, 12};
UINT OID_ListOps[]			     = {1, 1, 13};
UINT OID_SearchOps[]			 = {1, 1, 14};
UINT OID_OneLevelSearchOps[]	 = {1, 1, 15};
UINT OID_WholeSubtreeSearchOps[] = {1, 1, 16};
UINT OID_Referrals[]			 = {1, 1, 17};
UINT OID_Chainings[]			 = {1, 1, 18};
UINT OID_SecurityErrors[]		 = {1, 1, 19};
UINT OID_Errors[]			     = {1, 1, 20};

/* entries table */
UINT OID_MasterEntries[]		 = {2, 1, 1};
UINT OID_CopyEntries[]			 = {2, 1, 2};
UINT OID_CacheEntries[]			 = {2, 1, 3};
UINT OID_CacheHits[]			 = {2, 1, 4};
UINT OID_SlaveHits[]			 = {2, 1, 5};

/* interaction table */
UINT OID_DsIntIndex[]                = {3, 1, 1}; 
UINT OID_DsName[]			         = {3, 1, 2};
UINT OID_TimeOfCreation[]		     = {3, 1, 3};
UINT OID_TimeOfLastAttempt[]	     = {3, 1, 4};
UINT OID_TimeOfLastSuccess[]	     = {3, 1, 5};
UINT OID_FailuresSinceLastSuccess[]	 = {3, 1, 6};
UINT OID_Failures[]			         = {3, 1, 7};
UINT OID_Successes[]			     = {3, 1, 8};
UINT OID_URL[]				         = {3, 1, 9};

/* entity table */
UINT OID_EntityDescr[]           = {5, 1, 1};
UINT OID_EntityVers[]            = {5, 1, 2};
UINT OID_EntityOrg[]             = {5, 1, 3};
UINT OID_EntityLocation[]        = {5, 1, 4};
UINT OID_EntityContact[]         = {5, 1, 5};
UINT OID_EntityName[]            = {5, 1, 6};


/* make AsnObjectIdentifiers so can use snmputilOidcpy for each server instance, and append to put on indexes later */
/* ops table */
AsnObjectIdentifier ASN_AnonymousBinds               = {MAGT_OID_SIZEOF(OID_AnonymousBinds)       ,OID_AnonymousBinds};
AsnObjectIdentifier ASN_UnAuthBinds			         = {MAGT_OID_SIZEOF(OID_UnAuthBinds)          ,OID_UnAuthBinds};
AsnObjectIdentifier ASN_SimpleAuthBinds		         = {MAGT_OID_SIZEOF(OID_SimpleAuthBinds)      ,OID_SimpleAuthBinds}; 
AsnObjectIdentifier ASN_StrongAuthBinds		         = {MAGT_OID_SIZEOF(OID_StrongAuthBinds)      ,OID_StrongAuthBinds};
AsnObjectIdentifier ASN_BindSecurityErrors	         = {MAGT_OID_SIZEOF(OID_BindSecurityErrors)   ,OID_BindSecurityErrors};
AsnObjectIdentifier ASN_InOps			             = {MAGT_OID_SIZEOF(OID_InOps)                ,OID_InOps};
AsnObjectIdentifier ASN_ReadOps			             = {MAGT_OID_SIZEOF(OID_ReadOps)              ,OID_ReadOps};
AsnObjectIdentifier ASN_CompareOps			         = {MAGT_OID_SIZEOF(OID_CompareOps)           ,OID_CompareOps};
AsnObjectIdentifier ASN_AddEntryOps			         = {MAGT_OID_SIZEOF(OID_AddEntryOps)          ,OID_AddEntryOps};
AsnObjectIdentifier ASN_RemoveEntryOps		         = {MAGT_OID_SIZEOF(OID_RemoveEntryOps)       ,OID_RemoveEntryOps};
AsnObjectIdentifier ASN_ModifyEntryOps		         = {MAGT_OID_SIZEOF(OID_ModifyEntryOps)       ,OID_ModifyEntryOps};
AsnObjectIdentifier ASN_ModifyRDNOps		         = {MAGT_OID_SIZEOF(OID_ModifyRDNOps)         ,OID_ModifyRDNOps};
AsnObjectIdentifier ASN_ListOps			             = {MAGT_OID_SIZEOF(OID_ListOps)              ,OID_ListOps};
AsnObjectIdentifier ASN_SearchOps			         = {MAGT_OID_SIZEOF(OID_SearchOps)            ,OID_SearchOps};
AsnObjectIdentifier ASN_OneLevelSearchOps	         = {MAGT_OID_SIZEOF(OID_OneLevelSearchOps)    ,OID_OneLevelSearchOps};
AsnObjectIdentifier ASN_WholeSubtreeSearchOps        = {MAGT_OID_SIZEOF(OID_WholeSubtreeSearchOps),OID_WholeSubtreeSearchOps};
AsnObjectIdentifier ASN_Referrals			         = {MAGT_OID_SIZEOF(OID_Referrals)            ,OID_Referrals};
AsnObjectIdentifier ASN_Chainings			         = {MAGT_OID_SIZEOF(OID_Chainings)            ,OID_Chainings};
AsnObjectIdentifier ASN_SecurityErrors		         = {MAGT_OID_SIZEOF(OID_SecurityErrors)       ,OID_SecurityErrors};
AsnObjectIdentifier ASN_Errors			             = {MAGT_OID_SIZEOF(OID_Errors)               ,OID_Errors};

/* entries table */
AsnObjectIdentifier ASN_MasterEntries		         = {MAGT_OID_SIZEOF(OID_MasterEntries)        ,OID_MasterEntries};
AsnObjectIdentifier ASN_CopyEntries	   		         = {MAGT_OID_SIZEOF(OID_CopyEntries)          ,OID_CopyEntries};
AsnObjectIdentifier ASN_CacheEntries			     = {MAGT_OID_SIZEOF(OID_CacheEntries)         ,OID_CacheEntries};
AsnObjectIdentifier ASN_CacheHits			         = {MAGT_OID_SIZEOF(OID_CacheHits)            ,OID_CacheHits};
AsnObjectIdentifier ASN_SlaveHits			         = {MAGT_OID_SIZEOF(OID_SlaveHits)            ,OID_SlaveHits};

/* interaction table */
AsnObjectIdentifier ASN_DsName				         = {MAGT_OID_SIZEOF(OID_DsName)                   ,OID_DsName};
AsnObjectIdentifier ASN_TimeOfCreation			     = {MAGT_OID_SIZEOF(OID_TimeOfCreation)           ,OID_TimeOfCreation};
AsnObjectIdentifier ASN_TimeOfLastAttempt	         = {MAGT_OID_SIZEOF(OID_TimeOfLastAttempt)        ,OID_TimeOfLastAttempt};
AsnObjectIdentifier ASN_TimeOfLastSuccess	         = {MAGT_OID_SIZEOF(OID_TimeOfLastSuccess)        ,OID_TimeOfLastSuccess};
AsnObjectIdentifier ASN_FailuresSinceLastSuccess	 = {MAGT_OID_SIZEOF(OID_FailuresSinceLastSuccess) ,OID_FailuresSinceLastSuccess};
AsnObjectIdentifier ASN_Failures			         = {MAGT_OID_SIZEOF(OID_Failures)                 ,OID_Failures};
AsnObjectIdentifier ASN_Successes			         = {MAGT_OID_SIZEOF(OID_Successes)                ,OID_Successes};
AsnObjectIdentifier ASN_URL					         = {MAGT_OID_SIZEOF(OID_URL)                      ,OID_URL};

/* entity table */
AsnObjectIdentifier ASN_EntityDescr	                 = {MAGT_OID_SIZEOF(OID_EntityDescr)     ,OID_EntityDescr};
AsnObjectIdentifier ASN_EntityVers	                 = {MAGT_OID_SIZEOF(OID_EntityVers)      ,OID_EntityVers};
AsnObjectIdentifier ASN_EntityOrg	                 = {MAGT_OID_SIZEOF(OID_EntityOrg)       ,OID_EntityOrg};
AsnObjectIdentifier ASN_EntityLocation	             = {MAGT_OID_SIZEOF(OID_EntityLocation)  ,OID_EntityLocation};
AsnObjectIdentifier ASN_EntityContact	             = {MAGT_OID_SIZEOF(OID_EntityContact)   ,OID_EntityContact};
AsnObjectIdentifier ASN_EntityName                   = {MAGT_OID_SIZEOF(OID_EntityName)      ,OID_EntityName};

/*
 * Storage definitions for MIB.
 */
char szPlaceHolder[] = "Not Available";
int nPlaceHolder = 0;

#define NUM_ENTITY_COLUMNS     6
#define NUM_OPS_COLUMNS       20
#define NUM_ENTRIES_COLUMNS    5
#define NUM_INT_COLUMNS        8
#define NUM_INT_ROWS           5


UINT MIB_num_vars;

void OidAppendIndex(AsnObjectIdentifier *Oid, int Index);

int Mib_init(MagtMibEntry_t **Mib, int ApplIndex)
{
	int i;
	int j;
		
	MIB_num_vars = (UINT) 71;

	/* allocate the memory for this Mib Instance */
	if( (*Mib = (MagtMibEntry_t *) GlobalAlloc(GPTR, MIB_num_vars * 
		                            sizeof(MagtMibEntry_t ) )) == NULL)
	{
         	  return -1;
    }
		
	/**************************
	* Ops Table Stuff
	* --------------
	* AnonymousBinds         
	* UnAuthBinds            
	* SimpleAuthBinds        
	* StrongAuthBinds         
	* BindSecurityErrors    
	* InOps                 
	* ReadOps                
	* CompareOps            
	* AddEntryOps           
	* RemoveEntryOps        
	* ModifyEntryOps        
	* ModifyRDNOps          
	* ListOps               
	* SearchOps             
	* OneLevelSearchOps      
	* WholeSubtreeSearchOps 
	* Referrals             
	* Chainings             
	* SecurityErrors        
	* Errors                
    **************************/
	

	i=0;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_AnonymousBinds);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
  	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	    = NULL;
	(*Mib)[i].uId          = MAGT_ID_ANONYMOUS_BINDS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_UnAuthBinds);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
   	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	    = NULL;
	(*Mib)[i].uId          = MAGT_ID_UNAUTH_BINDS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_SimpleAuthBinds);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	    = NULL;
	(*Mib)[i].uId          = MAGT_ID_SIMPLE_AUTH_BINDS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_StrongAuthBinds);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
   	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_STRONG_AUTH_BINDS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_BindSecurityErrors);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_BIND_SECURITY_ERRORS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_InOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_IN_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_ReadOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_READ_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_CompareOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_COMPARE_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_AddEntryOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_ADD_ENTRY_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_RemoveEntryOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_REMOVE_ENTRY_OPS;
 
	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_ModifyEntryOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_MODIFY_ENTRY_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_ModifyRDNOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_MODIFY_RDN_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_ListOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_LIST_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_SearchOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_SEARCH_OPS;
 
	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_OneLevelSearchOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_ONE_LEVEL_SEARCH_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_WholeSubtreeSearchOps);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_WHOLE_SUBTREE_SEARCH_OPS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_Referrals);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_REFERRALS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_Chainings);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_CHAININGS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_SecurityErrors);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_SECURITY_ERRORS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_Errors);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_ERRORS;

	/**************************
	* Entries Table Stuff
	* --------------
	* MasterEntries  
	* CopyEntries    
	* CacheEntries   
	* CacheHits     
	* SlaveHits     
    **************************/

	i++;
    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_MasterEntries);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_MASTER_ENTRIES;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_CopyEntries);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_COPY_ENTRIES;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_CacheEntries);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_CACHE_ENTRIES;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_CacheHits);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_CACHE_HITS;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_SlaveHits);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = &nPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext      = NULL;
	(*Mib)[i].uId          = MAGT_ID_SLAVE_HITS;


	/**************************
	* Interaction Table Stuff
	* --------------
	* DsName
	* TimeOfCreation 
	* TimeOfLastAttempt
	* TimeOfLastSuccess
	* FailuresSinceLastSuccess
	* Failures
	* Successes
	* URL
    **************************/

    for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
		
	    i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_DsName);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
	  	(*Mib)[i].Storage      = szPlaceHolder;
	    (*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	    (*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	    (*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	    (*Mib)[i].MibNext	    = NULL;
	    (*Mib)[i].uId          = MAGT_ID_DS_NAME;

	}

	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	
	    i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_TimeOfCreation);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
	   	(*Mib)[i].Storage      = &nPlaceHolder;
	    (*Mib)[i].Type         = ASN_RFC1155_TIMETICKS;
	    (*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
    	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
    	(*Mib)[i].MibNext	    = NULL;
    	(*Mib)[i].uId          = MAGT_ID_TIME_OF_CREATION;
	}

	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	
    	i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_TimeOfLastAttempt);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
	   	(*Mib)[i].Storage      = &nPlaceHolder;
    	(*Mib)[i].Type         = ASN_RFC1155_TIMETICKS;
    	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
    	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
    	(*Mib)[i].MibNext	    = NULL;
    	(*Mib)[i].uId          = MAGT_ID_TIME_OF_LAST_ATTEMPT;
	}

	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	
    	i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_TimeOfLastSuccess);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
    	(*Mib)[i].Storage      = &nPlaceHolder;
    	(*Mib)[i].Type         = ASN_RFC1155_TIMETICKS;
    	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
    	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
    	(*Mib)[i].MibNext	    = NULL;
    	(*Mib)[i].uId          = MAGT_ID_TIME_OF_LAST_SUCCESS;
	}
    
	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	
		i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_FailuresSinceLastSuccess);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
    	(*Mib)[i].Storage      = &nPlaceHolder;
    	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
    	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
    	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
    	(*Mib)[i].MibNext	    = NULL;
    	(*Mib)[i].uId          = MAGT_ID_FAILURES_SINCE_LAST_SUCCESS;
	}
	
	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	   	i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_Failures);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
		(*Mib)[i].Storage      = &nPlaceHolder;
    	(*Mib)[i].Type         = ASN_RFC1155_COUNTER;
    	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
    	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
    	(*Mib)[i].MibNext	    = NULL;
    	(*Mib)[i].uId          = MAGT_ID_FAILURES;
	}
     
	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
		i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_Successes);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
		(*Mib)[i].Storage      = &nPlaceHolder;
	    (*Mib)[i].Type         = ASN_RFC1155_COUNTER;
	    (*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	    (*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	    (*Mib)[i].MibNext	    = NULL;
	    (*Mib)[i].uId          = MAGT_ID_SUCCESSES;
	}
	
	for(j=1; j <= NUM_SNMP_INT_TBL_ROWS; j++)
	{
	    i++;
  	    SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_URL);
	    OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
		OidAppendIndex(&((*Mib)[i].Oid), j);
		(*Mib)[i].Storage      = szPlaceHolder;
	    (*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	    (*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	    (*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	    (*Mib)[i].MibNext	    = NULL;
	    (*Mib)[i].uId          = MAGT_ID_URL;
	}

	/**************************
	* Entity Stuff 
	* --------------
	* EntityDescr       
	* EntityVers        
	* EntityOrg         
	* EntityLocation    
	* EntityContact     
	* EntityName        
    **************************/

	i++;
   	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityDescr);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	    = NULL;
	(*Mib)[i].uId          = MAGT_ID_DESC;
    
	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityVers);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_VERS;
  
	i++;
  	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityOrg);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_ORG;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityLocation);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_LOC;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityContact);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_CONTACT;

	i++;
	SnmpUtilOidCpy(&((*Mib)[i].Oid),  &ASN_EntityName);
	OidAppendIndex(&((*Mib)[i].Oid), ApplIndex);
	(*Mib)[i].Storage      = szPlaceHolder;
	(*Mib)[i].Type         = ASN_RFC1213_DISPSTRING;
	(*Mib)[i].Access       = MAGT_MIB_ACCESS_READ;
	(*Mib)[i].MibFunc      = MagtMIBLeafFunc;
	(*Mib)[i].MibNext	     = NULL;
	(*Mib)[i].uId          = MAGT_ID_NAME;

	return 0;
}

void OidAppendIndex(AsnObjectIdentifier *Oid, int Index)
{
	UINT OID_Index[1];             
	AsnObjectIdentifier ASN_Index;

	OID_Index[0] = Index;
	ASN_Index.ids = OID_Index;
	ASN_Index.idLength = 1;

	SnmpUtilOidAppend(Oid,  &ASN_Index);

}

/*-------------------------------------------------------------------------
 *
 * MagtFillTrapVars:  Fills in the variable list for the specified trap.
 *
 * Returns:  0 - No variable filled
 *           n - Number of variables filled
 *
 *-----------------------------------------------------------------------*/

int MagtFillTrapVars(int trapType, RFC1157VarBind *trapVars, MagtStaticInfo_t *pCfgInfo)
{
  MagtDispStr_t varVals[4];
  int nVarLen = 0;
  int i, j;
  static AsnObjectIdentifier varOid[4] = {{MAGT_OID_SIZEOF(OID_EntityDescr),
                                           OID_EntityDescr},
                                          {MAGT_OID_SIZEOF(OID_EntityVers),
                                           OID_EntityVers},
                                          {MAGT_OID_SIZEOF(OID_EntityLocation),
                                           OID_EntityLocation},
                                          {MAGT_OID_SIZEOF(OID_EntityContact),
                                           OID_EntityContact}};

  /*
   * Get the variable values from the static info which has been obtained
   * from the snmp config file at initialization time.
   */ 
  varVals[0].len = pCfgInfo->entityDescr.len;
  varVals[0].val = pCfgInfo->entityDescr.val;
  varVals[1].len = pCfgInfo->entityVers.len;
  varVals[1].val = pCfgInfo->entityVers.val;
  varVals[2].len = pCfgInfo->entityLocation.len;
  varVals[2].val = pCfgInfo->entityLocation.val;
  varVals[3].len = pCfgInfo->entityContact.len;
  varVals[3].val = pCfgInfo->entityContact.val;

  for (i = 0; i < 4; i++)
  {
      SNMP_oidcpy(&trapVars[i].name, &MIB_OidPrefix);
      SNMP_oidappend(&trapVars[i].name, &varOid[i]);
      trapVars[i].value.asnType = ASN_OCTETSTRING;
      trapVars[i].value.asnValue.string.length = varVals[i].len;
      trapVars[i].value.asnValue.string.stream =
          SNMP_malloc((trapVars[i].value.asnValue.string.length) *
              sizeof(char));
      if (trapVars[i].value.asnValue.string.stream == NULL)
      {

          /*
           * Clean up any allocated variable binding allocated up until now.
           */
          for (j = 0; j < i; j++)
          {
              SNMP_FreeVarBind(&trapVars[j]);  
              return nVarLen;
          }
      }
      memcpy(trapVars[i].value.asnValue.string.stream, varVals[i].val,
                 trapVars[i].value.asnValue.string.length);
      trapVars[i].value.asnValue.string.dynamic = TRUE;
  }

  switch (trapType)
  {
      case MAGT_TRAP_SERVER_DOWN:
          nVarLen = 4;
          break;
      case MAGT_TRAP_SERVER_START:
          nVarLen = 3;
          break;
      default:
          break;
  }

  return nVarLen;
}

/*-------------------------------------------------------------------------
 *
 * MagtMIBLeafFunc:  Performs generic actions on leaf variables in the MIB.
 *                   Note that SET action is not supported.
 *
 * Returns: SNMP_ERRORSTATUS_NOERROR - No error
 *          PDU error codes - Errors
 *
 *-----------------------------------------------------------------------*/

UINT MagtMIBLeafFunc(IN UINT Action, IN MagtMibEntry_t *mibPtr,
                     IN RFC1157VarBind *VarBind)
{
  UINT ErrStat = SNMP_ERRORSTATUS_NOERROR;
  static AsnObjectIdentifier ApplIndexOid = {MAGT_OID_SIZEOF(OID_ApplIndex),
                                         OID_ApplIndex};
  instance_list_t *pInstance;
  char logMsg[1024];

  switch(Action)
  {
    case MAGT_MIB_ACTION_GETNEXT:
      /*
       * If there is no next pointer, this is the end of the MIB tree.
       */
      if (mibPtr->MibNext == NULL)
      {
        ErrStat = SNMP_ERRORSTATUS_NOSUCHNAME;
        return ErrStat;
      }
      
      /*
       * Set up VarBind to contain the OID of the next variable.
       */
      SNMP_oidfree(&VarBind->name);
      SNMP_oidcpy(&VarBind->name, &MIB_OidPrefix);
      SNMP_oidappend(&VarBind->name, &mibPtr->MibNext->Oid);

      /*
       * Call function to process the GET request.
       */
      ErrStat = (*mibPtr->MibNext->MibFunc)(MAGT_MIB_ACTION_GET, mibPtr->MibNext,
                                            VarBind);
      break;
    case MAGT_MIB_ACTION_GET:
      
      /*
       * Make sure that the variable's access right allows GET.
       */
      if (mibPtr->Access != MAGT_MIB_ACCESS_READ &&
          mibPtr->Access != MAGT_MIB_ACCESS_READWRITE)
      {
        ErrStat = SNMP_ERRORSTATUS_NOSUCHNAME;
        return ErrStat;
      }
      
      if (mibPtr->Storage == NULL)		/* Counter not supported */
      {
        ErrStat = SNMP_ERRORSTATUS_GENERR;
        return ErrStat;
      }

      if ((VarBind->name.ids[MAGT_MIB_PREFIX_LEN] > 1) &&
          (SNMP_oidcmp(&mibPtr->Oid, &ApplIndexOid) != 0))
      {

            /*
             * Read stats file to update counter statistics.
             */

		  /* need to update all of them because don't know which instance resulted
			 into call into this function */
		  for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext)
          {
			  if (MagtReadStats(NULL, pInstance->pOpsStatInfo, 
									  pInstance->pEntriesStatInfo, 
									  pInstance->ppIntStatInfo, 
									  pInstance->szStatsPath, 
									  pInstance->szLogPath) != 0)
              {
                 
				  /* this server is off/or went down since we 
					 started up snmp. The snmp agent will
					 return last values it was set to until
					 server starts back up. If server was not
					 started will return null for strings and
					 0 for values */
				 
				  /* to log for each snmp request is to expensive
				     for now, just silently acknowledge the fact
					 and think about something better for the future
				   */
					
				  
              }
		  }
      }

      /*
       * Set up VarBind's return value.
       */
      VarBind->value.asnType = mibPtr->Type;
      switch (VarBind->value.asnType)
      {
        case ASN_RFC1155_TIMETICKS:
        case ASN_RFC1155_COUNTER:
        case ASN_RFC1155_GAUGE:
        case ASN_INTEGER:
          VarBind->value.asnValue.number = *(AsnInteger *)(mibPtr->Storage);
          break;
        case ASN_RFC1155_IPADDRESS:
        case ASN_OCTETSTRING:			/* = ASN_RFC1213_DISPSTRING */
          VarBind->value.asnValue.string.length =
            strlen((LPSTR)mibPtr->Storage);
          VarBind->value.asnValue.string.stream =
            SNMP_malloc((VarBind->value.asnValue.string.length + 2) * 
              sizeof(char));
          if (VarBind->value.asnValue.string.stream == NULL)
          {
            ErrStat = SNMP_ERRORSTATUS_GENERR;
            return ErrStat;
          }
          memcpy(VarBind->value.asnValue.string.stream,
                 (LPSTR)mibPtr->Storage,
                 VarBind->value.asnValue.string.length);
          VarBind->value.asnValue.string.dynamic = TRUE;
          break;
        case ASN_OBJECTIDENTIFIER:
          VarBind->value.asnValue.object = 
            *(AsnObjectIdentifier *)(mibPtr->Storage);
          break;
        default:
          ErrStat = SNMP_ERRORSTATUS_GENERR;
          break;
      }						/* Switch */

      break;
    default:
      ErrStat = SNMP_ERRORSTATUS_GENERR;
      break;
  }						/* Switch */
  
  return ErrStat;
}

/*-------------------------------------------------------------------------
 *
 * MagtResolveVarBind:  Resolves a single variable binding.  Modifies the
 *                      variable on a GET or a GETNEXT.
 *
 * Returns: SNMP_ERRORSTATUS_NOERROR - No error
 *          PDU error codes - Errors
 *
 *-----------------------------------------------------------------------*/

UINT MagtResolveVarBind(IN OUT RFC1157VarBind *VarBind, IN UINT PduAction)
{
  MagtMibEntry_t *mibPtr = NULL;
  AsnObjectIdentifier TempOid;
  int CompResult;
  UINT i = 0;
  UINT nResult;
  instance_list_t *pInstance;
 
	  pInstance = pInstanceList;

      while (mibPtr == NULL && pInstance !=NULL) 
      {		  
          /*
           * Construct OID with complete prefix for comparison purpose
           */
          SNMP_oidcpy(&TempOid, &MIB_OidPrefix);
          SNMP_oidappend(&TempOid, &(pInstance->pMibInfo[i].Oid));
    
          /*
           * Check for OID in MIB.  On a GET-NEXT, the OID does not have to match
           * exactly a variable in the MIB, it must only fall under the MIB root.
           */
           CompResult = SNMP_oidcmp(&VarBind->name, &TempOid); 
		   
           if (CompResult < 0)				/* Not an exact match */
           {
               if (PduAction != MAGT_MIB_ACTION_GETNEXT)	/* Only GET-NEXT is valid */
               {

				   pInstance=pInstance->pNext;
				   i=0;
				   if(pInstance == NULL)
				   {
                       nResult = SNMP_ERRORSTATUS_NOSUCHNAME;
                       return nResult;
				   }else{
					   continue;
				   }

				}
      
               /*
                * Since the match was not exact, but var bind name is within MIB,
                * we are at the next MIB variable down from the one specified.
                */
                PduAction = MAGT_MIB_ACTION_GET;
                mibPtr = &(pInstance->pMibInfo[i]);

               /*
                * Replace var bind name with new name.
                */
               SNMP_oidfree(&VarBind->name);
               SNMP_oidcpy(&VarBind->name, &MIB_OidPrefix);
               SNMP_oidappend(&VarBind->name, &mibPtr->Oid);
           }
           else
           {
			   if (CompResult == 0)			/* Found an exact match */
               {
			       mibPtr = &(pInstance->pMibInfo[i]);
		       }else{
			       /* see if it is one of the other ApplIndex */
				   instance_list_t *pApplIndex;
				   for(pApplIndex = pInstance; pApplIndex; pApplIndex=pApplIndex->pNext)
				   {
					   SNMP_oidfree(&TempOid);

					   SNMP_oidcpy(&TempOid, &MIB_OidPrefix);
                       SNMP_oidappend(&TempOid, &(pApplIndex->pMibInfo[i].Oid));

					   CompResult = SNMP_oidcmp(&VarBind->name, &TempOid); 
					   if(CompResult == 0)
					   {
						   mibPtr = &(pApplIndex->pMibInfo[i]);
					   }
				   }
			   }


           }

           /*
            * Free OID memory before checking another variable.
            */
           SNMP_oidfree(&TempOid);
           i++;
		   
		   if(i == MIB_num_vars)
		   {
		       pInstance=pInstance->pNext;
			   i=0;
		   }
       }						/* While */

       if (mibPtr == NULL)				/* OID not within MIB's scope */
       {
		   nResult = SNMP_ERRORSTATUS_NOSUCHNAME;
           return nResult;
       }

       if (*mibPtr->MibFunc == NULL)
       {
           nResult = SNMP_ERRORSTATUS_NOSUCHNAME;
           return nResult;
       }
     
       /*
        * Call function to process request.  Each MIB entry has a function pointer
        * that knows how to process its MIB variable.
        */
       nResult = (*mibPtr->MibFunc)(PduAction, mibPtr, VarBind);

       SNMP_oidfree(&TempOid);			/* Free temp memory */
  
       return nResult;
  
}

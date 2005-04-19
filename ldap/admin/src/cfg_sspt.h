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
#ifndef __cfg_sspt_h
#define __cfg_sspt_h

#ifdef __cplusplus
extern "C" {            /* Assume C declarations for C++ */
#endif  /* __cplusplus */

#include "ldap.h"
#include "dsalib.h" 

#define MAX_STRING_LEN 512

typedef struct _SLAPD_CONFIG {
  char  slapd_server_root[MAX_STRING_LEN + 1];
  int   port;
  char  host[MAX_STRING_LEN];
  char  root_dn[MAX_STRING_LEN];
#define MAX_SUFFIXES 1024
  char* suffixes[MAX_SUFFIXES];
  int   num_suffixes;
} SLAPD_CONFIG;

typedef struct _query_vars {
  char* suffix;
  char* ssAdmID;
  char* ssAdmPW1;
  char* ssAdmPW2;
  char* rootDN;
  char* rootPW;
  char* consumerDN;
  char* consumerPW;
  char* netscaperoot;
  char* testconfig;
  char* admin_domain;
  int cfg_sspt;
  char* config_admin_uid;
} QUERY_VARS;

extern int
entry_exists(LDAP* ld, const char* entrydn);

extern int
config_suitespot(SLAPD_CONFIG* slapd, QUERY_VARS* query);

extern int
create_group(LDAP* ld, char* base, char* group);

#ifndef __CFG_SSPT_C

extern char* const class_top;
extern char* const class_organization;
extern char* const class_organizationalUnit;
extern char* const class_person;
extern char* const class_organizationalPerson;
extern char* const class_inetOrgPerson;
extern char* const class_groupOfUniqueNames;

extern char* const name_objectClass;
extern char* const name_cn;
extern char* const name_sn;
extern char* const name_givenname;
extern char* const name_uid;
extern char* const name_userPassword;
extern char* const name_o;
extern char* const name_ou;
extern char* const name_member;
extern char* const name_uniqueMember;
extern char* const name_subtreeaci;
extern char* const name_netscaperoot;
extern char* const name_netscaperootDN;

extern char* const value_suiteSpotAdminCN;
extern char* const value_suiteSpotAdminSN;
extern char* const value_suiteSpotAdminGN;
extern char* const value_adminGroupCN;
extern char* const value_netscapeServersOU;

extern char* const field_suffix;
extern char* const field_ssAdmID;
extern char* const field_ssAdmPW1;
extern char* const field_ssAdmPW2;
extern char* const field_rootDN;
extern char* const field_rootPW;
extern char* const format_DN;
extern char* const format_simpleSearch;

extern char* const insize_text;

extern char* html_file;
extern char* dbg_log_file;

#endif /* __CFG_SSPT_C */

/*
 * iterate over the root DSEs we need to setup special ACIs for
 * return true if entry and access are valid, false when the list
 * is empty and entry and access are null
 */
int getEntryAndAccess(int index, const char **entry, const char **access);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* __cfg_sspt_h */

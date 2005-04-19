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
/* lite_entries.c - 
 *
 * These entries are added under cn=options,cn=features,cn=config for 
 * Directory Lite. These entries tell the console which modules ( ds features )
 * are disabled, and which attributes in cn=config are disabled.
 */

   
   

#include "slap.h"

static void  del_dslite_entries();
static void  add_dslite_entries();
static const char *lite_entries[] = 
{
  "dn:" LITE_DISABLED_ATTRS_DN "\n"
  "cn:attributes\n"
  "objectclass:top\n"
  "objectclass:extensibleObject\n"
  "objectclass:directoryServerFeature\n"
  "cn=config|nsslapd-referral:off\n"
  "cn=config|nsslapd-maxdescriptors:off\n",
  
  "dn:" LITE_DISABLED_MODULES_DN "\n"
  "objectclass:top\n"
  "objectclass:directoryServerFeature\n"
  "objectclass:extensibleObject\n"
  "cn:modules\n"
  "replication:off\n"
  "passwordpolicy:off\n"
  "accountlockout:off\n"
  "snmpsettings:off\n"
  "backup:off\n",

  NULL
};

/* add_dslite_entries: 
 *
 * Add the DirLite specific entries.
 * First we try to delete them in case they were already loaded from dse.ldif
 * but are in some sort of invalid state.
 * Then we add them back.
 * It would have been better to make sure that these entries never get written
 * to dse.ldif, but it doesn't look like we're able to add a dse_callback function
 * on the DN's of these entries but they get added at the wrong time.
 */
   
   
   
static void
add_dslite_entries() {
  int i;

  del_dslite_entries();
  LDAPDebug( LDAP_DEBUG_TRACE, "Adding lite entries.\n",0,0,0);
  for( i = 0; lite_entries[i]; i++ ) {
	Slapi_PBlock *resultpb;
	char *estr = slapi_ch_strdup ( lite_entries[i] );
  	Slapi_Entry *e = slapi_str2entry( estr, 0 );
	if ( NULL != e ) {
	  resultpb = slapi_add_entry_internal( e, NULL, 0 );
	  slapi_ch_free ( (void **) &resultpb );
	  slapi_ch_free ( (void **) &estr );
	}
  }
}


/* del_dslite_entries: delete the DirLite specific entries */
static void 
del_dslite_entries() {
  Slapi_PBlock *resultpb;
  LDAPDebug( LDAP_DEBUG_TRACE, "Deleting lite entries if they exist\n",0,0,0);
  resultpb = slapi_delete_internal (  LITE_DISABLED_ATTRS_DN, NULL, 0 );
  slapi_pblock_destroy ( resultpb );
  resultpb = slapi_delete_internal (  LITE_DISABLED_MODULES_DN,  NULL, 0 );
  slapi_pblock_destroy ( resultpb );
}

/* lite_entries_init()
 * Add the appropriate entries under cn=options,cn=features,cn=config if the
 * server is running as Directory Lite.
 *
 * Otherwise, if the server is the Full Directory, try to delete the entries if
 * they're already there.
 */
   
void
lite_entries_init() {
  if ( config_is_slapd_lite() ) {
	add_dslite_entries();
  }
  else {
	del_dslite_entries();
  }
}
  
  


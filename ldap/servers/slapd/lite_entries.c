/** BEGIN COPYRIGHT BLOCK
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
  
  


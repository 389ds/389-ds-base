/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "retrocl.h"

/* 
The changelog is created by 
 - changing the node in the dse tree which represents the changelog plugin
   to enabled on,
 - shutting down the server,
 - starting the server.
 */

/******************************/

/*
 * Function: retrocl_create_be
 *
 * Returns: LDAP_
 * 
 * Arguments: location in file system to put changelog, or NULL for default
 *
 * Description: 
 * add an entry of class nsBackendInstance below cn=ldbm,cn=plugins,cn=config
 *
 */

static int retrocl_create_be(const char *bedir)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry *e;
    struct berval *vals[2];
    struct berval val;
    int rc;

    vals[0] = &val;
    vals[1] = NULL; 

    e = slapi_entry_alloc();
    slapi_entry_set_dn(e,slapi_ch_strdup(RETROCL_LDBM_DN));

    /* Set the objectclass attribute */
    val.bv_val = "top";
    val.bv_len = 3;
    slapi_entry_add_values( e, "objectclass", vals );

    /* Set the objectclass attribute */
    val.bv_val = "extensibleObject";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    /* Set the objectclass attribute */
    val.bv_val = "nsBackendInstance";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    val.bv_val = "changelog";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "cn", vals );

    val.bv_val = RETROCL_BE_CACHESIZE;
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-cachesize", vals );

    val.bv_val = RETROCL_CHANGELOG_DN;
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-suffix", vals );

    val.bv_val = RETROCL_BE_CACHEMEMSIZE;
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-cachememsize", vals );

    val.bv_val = "off";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-readonly", vals );
    
    if (bedir) {
        val.bv_val = (char *)bedir;  /* cast const */
	val.bv_len = strlen(val.bv_val);
	slapi_entry_add_values( e, "nsslapd-directory", vals );
    }

    pb = slapi_pblock_new ();
    slapi_add_entry_internal_set_pb( pb, e, NULL /* controls */, 
				     g_plg_identity[PLUGIN_RETROCL], 
				     0 /* actions */ );
    slapi_add_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    slapi_pblock_destroy(pb);
    
    if (rc == 0) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "created changelog database node\n");
    } else if (rc == LDAP_ALREADY_EXISTS) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "changelog database node already existed\n");
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "Changelog LDBM backend could not be created (%d)\n", rc);
	return rc;
    }


    /* we need the changenumber indexed */
    e = slapi_entry_alloc();
    slapi_entry_set_dn(e,slapi_ch_strdup(RETROCL_INDEX_DN));

    /* Set the objectclass attribute */
    val.bv_val = "top";
    val.bv_len = 3;
    slapi_entry_add_values( e, "objectclass", vals );

    /* Set the objectclass attribute */
    val.bv_val = "nsIndex";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    val.bv_val = "changenumber";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "cn", vals );

    val.bv_val = "false";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nssystemindex", vals );

    val.bv_val = "eq";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsindextype", vals );

    pb = slapi_pblock_new ();
    slapi_add_entry_internal_set_pb( pb, e, NULL /* controls */, 
				     g_plg_identity[PLUGIN_RETROCL], 
				     0 /* actions */ );
    slapi_add_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    slapi_pblock_destroy(pb);
    
    if (rc == 0) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "created changenumber index node\n");
    } else if (rc == LDAP_ALREADY_EXISTS) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "changelog index node already existed\n");
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "Changelog LDBM backend changenumber index could not be created (%d)\n", rc);
	return rc;
    }

    return rc;
}

/*
 * Function: retrocl_create_config
 *
 * Returns: LDAP_
 * 
 * Arguments: none
 *
 * Description:
 * This function is called if there was no mapping tree node or backend for 
 * cn=changelog.
 */
int retrocl_create_config(void)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry *e;
    struct berval *vals[2];
    struct berval val;
    int rc;

    vals[0] = &val;
    vals[1] = NULL; 

    /* Assume the mapping tree node is missing.  It doesn't hurt to 
     * attempt to add it if it already exists.  You will see a warning
     * in the errors file when the referenced backend does not exist.
     */
    e = slapi_entry_alloc();
    slapi_entry_set_dn(e,slapi_ch_strdup(RETROCL_MAPPINGTREE_DN));
    
    /* Set the objectclass attribute */
    val.bv_val = "top";
    val.bv_len = 3;
    slapi_entry_add_values( e, "objectclass", vals );

    
    /* Set the objectclass attribute */
    val.bv_val = "extensibleObject";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    /* Set the objectclass attribute */
    val.bv_val = "nsMappingTree";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    val.bv_val = "backend";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-state", vals );

    val.bv_val = RETROCL_CHANGELOG_DN;
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "cn", vals );

    val.bv_val = "changelog";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "nsslapd-backend", vals );
    
    pb = slapi_pblock_new ();
    slapi_add_entry_internal_set_pb( pb, e, NULL /* controls */, 
				     g_plg_identity[PLUGIN_RETROCL], 
				     0 /* actions */ );
    slapi_add_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    slapi_pblock_destroy(pb);
    
    if (rc == 0) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "created changelog mapping tree node\n");
    } else if (rc == LDAP_ALREADY_EXISTS) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "changelog mapping tree node already existed\n");
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "cn=\"cn=changelog\",cn=mapping tree,cn=config could not be created (%d)\n", rc);
	return rc;
    }

    retrocl_be_changelog = slapi_be_select_by_instance_name("changelog");

    if (retrocl_be_changelog == NULL) {
        /* This is not the nsslapd-changelogdir from cn=changelog4,cn=config */
        char *bedir;

	bedir = retrocl_get_config_str(CONFIG_CHANGELOG_DIRECTORY_ATTRIBUTE);
	
	if (bedir == NULL) {
	    /* none specified */
	}

	rc = retrocl_create_be(bedir);
	slapi_ch_free ((void **)&bedir);
	if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
	    return rc;
	}

	retrocl_be_changelog = slapi_be_select_by_instance_name("changelog");
    }

    return LDAP_SUCCESS;
}

/******************************/

/* Function: retrocl_create_cle
 *
 * Arguments: none
 * Returns: nothing
 * Description: Attempts to create the cn=changelog entry which might already
 * exist.
 */

void retrocl_create_cle (void)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry *e;
    int rc;
    struct berval *vals[2];
    struct berval val;

    vals[0] = &val;
    vals[1] = NULL;

    e = slapi_entry_alloc();
    slapi_entry_set_dn(e,slapi_ch_strdup(RETROCL_CHANGELOG_DN));
    
    /* Set the objectclass attribute */
    val.bv_val = "top";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );

    
    /* Set the objectclass attribute */
    val.bv_val = "nsContainer";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "objectclass", vals );
    

    /* Set the objectclass attribute */
    val.bv_val = "changelog";
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "cn", vals );  
    
    val.bv_val = RETROCL_ACL;
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values( e, "aci", vals );  

    pb = slapi_pblock_new ();
    slapi_add_entry_internal_set_pb( pb, e, NULL /* controls */, 
				     g_plg_identity[PLUGIN_RETROCL], 
				     0 /* actions */ );
    slapi_add_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    slapi_pblock_destroy(pb);
    
    if (rc == 0) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "created cn=changelog\n");
    } else if (rc == LDAP_ALREADY_EXISTS) {
        slapi_log_error (SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
			 "cn=changelog already existed\n");
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "cn=changelog could not be created (%d)\n", rc);
    }
}


/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if 0
int detached;
int error_logfp;
#endif

main()
{
	ldbm_back_bind();
	ldbm_back_unbind();
	ldbm_back_search();
	ldbm_back_compare();
	ldbm_back_modify();
	ldbm_back_modrdn();
	ldbm_back_add();
	ldbm_back_delete();
	ldbm_back_abandon();
	ldbm_back_config();
	ldbm_back_init();
	ldbm_back_close();
	ldbm_back_flush();
}

#if 0
slapi_access_allowed(){}
send_ldap_result(){}
slapi_op_abandoned(){}
be_issuffix(){}
slapi_pw_find(){}
send_ldap_search_entry(){}
slapi_pblock_get(){}
slapi_pblock_set(){}
slapi_acl_check_mods(){}
#endif

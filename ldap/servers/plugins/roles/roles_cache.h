/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#if !defined( _ROLES_CACHE_H )

#define SLAPD_ROLES_INTERFACE "roles-slapd"
#define ROLES_PLUGIN_SUBSYSTEM   "roles-plugin"
#define NSROLEATTR "nsRole"

#define ROLE_DEFINITION_FILTER "(&(objectclass=nsRoleDefinition)(objectclass=ldapsubentry))"
#define OBJ_FILTER "(|(objectclass=*)(objectclass=ldapsubentry))"

#define ROLE_TYPE_MANAGED 1
#define ROLE_TYPE_FILTERED 2
#define ROLE_TYPE_NESTED 3

#define ROLE_OBJECTCLASS_MANAGED "nsManagedRoleDefinition"
#define ROLE_OBJECTCLASS_FILTERED "nsFilteredRoleDefinition"
#define ROLE_OBJECTCLASS_NESTED "nsNestedRoleDefinition"

#define ROLE_FILTER_ATTR_NAME "nsRoleFilter"
#define ROLE_MANAGED_ATTR_NAME "nsRoleDN"
#define ROLE_NESTED_ATTR_NAME "nsRoleDN"

#define SLAPI_ROLE_ERROR_NO_FILTER_SPECIFIED -1
#define SLAPI_ROLE_ERROR_FILTER_BAD -2
#define SLAPI_ROLE_DEFINITION_DOESNT_EXIST -3
#define SLAPI_ROLE_DEFINITION_ERROR -4
#define SLAPI_ROLE_DEFINITION_ALREADY_EXIST -5

/* From roles_cache.c */
int roles_cache_init();
void roles_cache_stop();
void roles_cache_change_notify(Slapi_PBlock *pb);
int roles_cache_listroles(Slapi_Entry *entry, int return_value, Slapi_ValueSet **valueset_out);

int roles_check(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present);

/* From roles_plugin.c */
int roles_init( Slapi_PBlock *pb );
int roles_sp_get_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *free_flags, void *hint);

int roles_sp_compare_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result,int flags, void *hint);

int roles_sp_list_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags);

void * roles_get_plugin_identity();

#endif /* _ROLES_CACHE_H */

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


/* Defines the vattr SPI interface, used by COS and Roles at present */
/* Also needs to be included by any code which participates in the vattr
   loop detection scheme (e.g. filter test code) 
 */

/* Loop context structure */
typedef struct _vattr_context vattr_context;
typedef struct _vattr_sp_handle vattr_sp_handle;
typedef struct _vattr_type_list_context vattr_type_list_context;

typedef int (*vattr_get_fn_type)(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *free_flags, void *hint);
typedef int (*vattr_get_ex_fn_type)(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char **type, Slapi_ValueSet*** results,int **type_name_disposition, char*** actual_type_name, int flags, int *free_flags, void **hint);
typedef int (*vattr_compare_fn_type)(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result, int flags, void *hint);
typedef int (*vattr_types_fn_type)(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags);

vattr_context *vattr_context_new( Slapi_PBlock *pb );

int slapi_vattrspi_register(vattr_sp_handle **h, vattr_get_fn_type get_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn);

/* options must be set to null */
int slapi_vattrspi_register_ex(vattr_sp_handle **h, vattr_get_ex_fn_type get_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn, void *options);
int slapi_vattrspi_regattr(vattr_sp_handle *h,char *type_name_to_register, char* DN /* Is there a DN type ?? */, void *hint);

/* Type thang structure used by slapi_vattrspi_add_type() */
struct _vattr_type_thang {
	char		*type_name;
	unsigned long	type_flags;   /* Same values as Slapi_Attr->a_flags */
	Slapi_ValueSet	*type_values; /* for slapi_vattr_list_attrs() use only */
};

int slapi_vattrspi_add_type(vattr_type_list_context *c, vattr_type_thang *thang, int flags);

/* get thang structure used by slapi_vattr_values_get_sp() */
struct _vattr_get_thang {
	int get_present;
	char *get_type_name;
	int get_name_disposition;
	Slapi_ValueSet *get_present_values;
	Slapi_Attr *get_attr;
};

/* Loop-detection-aware versions of the functions, to be called by service providers and their ilk */
SLAPI_DEPRECATED int slapi_vattr_values_get_sp(vattr_context *c, /* Entry we're interested in */ Slapi_Entry *e, /* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet** results,int *type_name_disposition, char **actual_type_name, int flags, int *free_flags);
int slapi_vattr_values_get_sp_ex(vattr_context *c, /* Entry we're interested in */ Slapi_Entry *e, /* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet*** results,int **type_name_disposition, char ***actual_type_name, int flags, int *free_flags, int *subtype_count);
int slapi_vattr_namespace_values_get_sp(vattr_context *c, /* Entry we're interested in */ Slapi_Entry *e, /* backend namespace dn */ Slapi_DN *namespace_dn, /* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet*** results,int **type_name_disposition, char ***actual_type_name, int flags, int *free_flags, int *subtype_count);
int slapi_vattr_value_compare_sp(vattr_context *c, Slapi_Entry *e,char *type, Slapi_Value *test_this,  int *result, int flags);
int slapi_vattr_namespace_value_compare_sp(vattr_context *c,/* Entry we're interested in */ Slapi_Entry *e, /* backend namespace dn*/Slapi_DN *namespace_dn, /* attr type name */ const char *type, Slapi_Value *test_this,/* pointer to result */ int *result, int flags);


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


/*
 * Netscape Directory Server 4.x plugin API backwards compatible header file.
 */
#ifndef _SLAPIPLUGINCOMPAT4
#define _SLAPIPLUGINCOMPAT4

#ifdef __cplusplus
extern "C" {
#endif


/******* Deprecated (obsolete) macros ****************************************/
/*
 * The SLAPI_BIND_FAIL_OR_ANONYMOUS bind function return code is deprecated.
 * Use SLAPI_BIND_FAIL or SLAPI_BIND_ANONYMOUS instead.
 */
#define SLAPI_BIND_FAIL_OR_ANONYMOUS	1    /* back end should send result */


/******* Deprecated (obsolete) pblock arg identifiers ************************/
#define SLAPI_CONFIG_FILENAME		40	/* use config. entries instead */
#define SLAPI_CONFIG_LINENO			41	/* use config. entries instead */
#define SLAPI_CONFIG_ARGC			42	/* use config. entries instead */
#define SLAPI_CONFIG_ARGV			43	/* use config. entries instead */
#define SLAPI_CONN_AUTHTYPE    		144	/* use SLAPI_CONN_AUTHTYPE */
#define SLAPI_REQUESTOR_ISUPDATEDN	SLAPI_IS_REPLICATED_OPERATION
#define SLAPI_CONN_CLIENTIP			145	/* use SLAPI_CONN_CLIENTNETADDR */
#define SLAPI_CONN_SERVERIP			146	/* use SLAPI_CONN_SERVERNETADDR */

/******* Deprecated (obsolete) functions *************************************/
/*
 * Please use the new functions in slapi-plugin.h instead of the ones
 * below that are marked SLAPI_DEPRECATED.
 */
#define SLAPI_DEPRECATED


/*
 *
 * The following functions that deal with bervals are deprecated
 * and their use is not recommended.  For each deprecated function, you
 * will find in slapi-plugin.h a corresponding function with a _sv
 * extension that uses Slapi_Values instead of bervals.  The new
 * functions are more efficient than the old ones, and some of the old
 * functions are much less efficient than they were previously.
 */
SLAPI_DEPRECATED int slapi_entry_attr_merge( Slapi_Entry *e, const char *type,
		struct berval **vals );
SLAPI_DEPRECATED int slapi_entry_add_values( Slapi_Entry *e, const char *type,
		struct berval **vals );
SLAPI_DEPRECATED int slapi_entry_delete_values( Slapi_Entry *e,
		const char *type, struct berval **vals );
SLAPI_DEPRECATED int slapi_entry_attr_replace( Slapi_Entry *e,
		const char *type, struct berval **vals );
SLAPI_DEPRECATED int slapi_attr_get_values( Slapi_Attr *attr,
		struct berval ***vals );
SLAPI_DEPRECATED int slapi_attr_get_oid( const Slapi_Attr *attr, char **oid );
SLAPI_DEPRECATED int slapi_filter_get_type( Slapi_Filter *f, char **type );
SLAPI_DEPRECATED int slapi_pw_find( struct berval **vals, struct berval *v );
SLAPI_DEPRECATED int slapi_call_syntax_values2keys( void *vpi,
		struct berval **vals, struct berval ***ivals, int ftype );
SLAPI_DEPRECATED int slapi_call_syntax_assertion2keys_ava( void *vpi,
		struct berval *val, struct berval ***ivals, int ftype );
SLAPI_DEPRECATED int slapi_call_syntax_assertion2keys_sub( void *vpi,
		char *initial, char **any, char *final, struct berval ***ivals );
SLAPI_DEPRECATED int slapi_attr_values2keys( const Slapi_Attr *sattr,
		struct berval **vals, struct berval ***ivals, int ftype );
SLAPI_DEPRECATED int slapi_attr_assertion2keys_ava( const Slapi_Attr *sattr,
		struct berval *val, struct berval ***ivals, int ftype );
SLAPI_DEPRECATED int slapi_attr_assertion2keys_sub( const Slapi_Attr *sattr,
		char *initial, char **any, char *final, struct berval ***ivals );

/*
 * slapi_entry_attr_hasvalue() has been deprecated in favor of
 * slapi_entry_attr_has_syntax_value() which respects the syntax of the
 * attribute type (slapi_entry_attr_hasvalue() assumes the value is a
 * caseIgnore ASCII string).
 */
SLAPI_DEPRECATED int slapi_entry_attr_hasvalue(const Slapi_Entry *e,
		const char *type, const char *value);

/*
 * The following "internal operation" calls are deprecated.  The new internal
 * operation functions that are defined in slapi-plugin.h take a Slapi_PBlock
 * for extensibility and support the new plugin configuration capabilities
 * as well.
 */
SLAPI_DEPRECATED int slapi_search_internal_callback( const char *ibase,
		int scope, const char *ifstr, char **attrs, int attrsonly,
		void *callback_data, LDAPControl **controls,
		plugin_result_callback prc, plugin_search_entry_callback psec,
		plugin_referral_entry_callback prec );
SLAPI_DEPRECATED int slapi_seq_callback( const char *ibase, int type,
		char *attrname, char *val, char **attrs, int attrsonly,
		void *callback_data, LDAPControl **controls,
		plugin_result_callback res_callback,
		plugin_search_entry_callback srch_callback,
		plugin_referral_entry_callback ref_callback);
SLAPI_DEPRECATED Slapi_PBlock *slapi_search_internal( const char *base,
		int scope, const char *filter, LDAPControl **controls, char **attrs,
		int attrsonly );
SLAPI_DEPRECATED Slapi_PBlock *slapi_modify_internal( const char *dn,
		LDAPMod **mods, LDAPControl **controls, int dummy );
SLAPI_DEPRECATED Slapi_PBlock *slapi_add_internal( const char * dn,
		LDAPMod **attrs, LDAPControl **controls, int dummy );
SLAPI_DEPRECATED Slapi_PBlock *slapi_add_entry_internal( Slapi_Entry *e,
		LDAPControl **controls, int dummy );
SLAPI_DEPRECATED Slapi_PBlock *slapi_delete_internal( const char * dn,
		LDAPControl **controls, int dummy );
SLAPI_DEPRECATED Slapi_PBlock *slapi_modrdn_internal( const char * olddn,
		const char * newrdn, int deloldrdn, LDAPControl **controls, int dummy);
SLAPI_DEPRECATED Slapi_PBlock *slapi_rename_internal( const char *iodn,
		const char *inewrdn, const char *inewsuperior, int deloldrdn,
		LDAPControl **controls, int dummy);

/*
 * These following three functions are not multi-thread (MT) safe (they return
 * a pointer to unprotected data).  Use the new functions from slapi-plugin.h
 * that end in _copy() instead, e.g., use slapi_get_supported_controls_copy()
 * instead of slapi_get_supported_controls().
 */
SLAPI_DEPRECATED int slapi_get_supported_controls( char ***ctrloidsp,
	unsigned long **ctrlopsp );
SLAPI_DEPRECATED char **slapi_get_supported_extended_ops( void );
SLAPI_DEPRECATED char **slapi_get_supported_saslmechanisms( void );


#ifdef __cplusplus
}
#endif

#endif /* _SLAPIPLUGINCOMPAT4 */

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

/* slapi-plugin.h - public Directory Server plugin interface */

#ifndef _SLAPIPLUGIN
#define _SLAPIPLUGIN

#ifdef __cplusplus
extern "C" {
#endif

#include "prtypes.h"
#include "ldap.h"

/*
 * The slapi_attr_get_flags() routine returns a bitmap that contains one or
 * more of these values.
 *
 * Note that the flag values 0x0010, 0x0020, 0x4000, and 0x8000 are reserved.
 */
#define SLAPI_ATTR_FLAG_SINGLE		0x0001	/* single-valued attribute */
#define SLAPI_ATTR_FLAG_OPATTR		0x0002	/* operational attribute */
#define SLAPI_ATTR_FLAG_READONLY	0x0004	/* read from shipped config file */
#define SLAPI_ATTR_FLAG_STD_ATTR	SLAPI_ATTR_FLAG_READONLY /* alias for read only */
#define SLAPI_ATTR_FLAG_OBSOLETE	0x0040	/* an outdated definition */
#define SLAPI_ATTR_FLAG_COLLECTIVE	0x0080	/* collective (not supported) */
#define SLAPI_ATTR_FLAG_NOUSERMOD	0x0100	/* can't be modified over LDAP */

/* operation flags */
#define SLAPI_OP_FLAG_NEVER_CHAIN	0x00800 /* Do not chain the operation */	
#define SLAPI_OP_FLAG_NO_ACCESS_CHECK  	0x10000 /* Do not check for access control - bypass them */

/*
 * access control levels
 */
#define SLAPI_ACL_COMPARE     	0x01
#define SLAPI_ACL_SEARCH      	0x02
#define SLAPI_ACL_READ        	0x04
#define SLAPI_ACL_WRITE		0x08
#define SLAPI_ACL_DELETE	0x10
#define SLAPI_ACL_ADD		0x20
#define SLAPI_ACL_SELF		0x40
#define SLAPI_ACL_PROXY		0x80
#define SLAPI_ACL_ALL		0x7f


/*
 * filter types
 */
#define LDAP_FILTER_AND		0xa0L
#define LDAP_FILTER_OR		0xa1L
#define LDAP_FILTER_NOT		0xa2L
#define LDAP_FILTER_EQUALITY	0xa3L
#define LDAP_FILTER_SUBSTRINGS	0xa4L
#define LDAP_FILTER_GE		0xa5L
#define LDAP_FILTER_LE		0xa6L
#define LDAP_FILTER_PRESENT	0x87L
#define LDAP_FILTER_APPROX	0xa8L
#define LDAP_FILTER_EXTENDED	0xa9L


/*
 * Sequential access types
 */
#define	SLAPI_SEQ_FIRST		1
#define	SLAPI_SEQ_LAST		2
#define	SLAPI_SEQ_PREV		3
#define	SLAPI_SEQ_NEXT		4


/*
 * return codes from a backend API call
 */
#define SLAPI_FAIL_GENERAL	-1
#define SLAPI_FAIL_DISKFULL	-2


/*
 * return codes used by BIND functions
 */
#define SLAPI_BIND_SUCCESS		0    /* front end will send result */
#define SLAPI_BIND_FAIL			2    /* back end should send result */
#define SLAPI_BIND_ANONYMOUS		3    /* front end will send result */


/* commonly used attributes names */
#define SLAPI_ATTR_UNIQUEID			"nsuniqueid"
#define SLAPI_ATTR_OBJECTCLASS			"objectclass"
#define SLAPI_ATTR_VALUE_TOMBSTONE		"nsTombstone"
#define SLAPI_ATTR_VALUE_PARENT_UNIQUEID	"nsParentUniqueID"
#define SLAPI_ATTR_NSCP_ENTRYDN "nscpEntryDN"


/* opaque structures */
typedef struct slapi_pblock		Slapi_PBlock;
typedef struct slapi_entry		Slapi_Entry;
typedef struct slapi_attr		Slapi_Attr;
typedef struct slapi_value  		Slapi_Value;
typedef struct slapi_value_set  	Slapi_ValueSet;
typedef struct slapi_filter		Slapi_Filter;
typedef struct slapi_matchingRuleEntry	Slapi_MatchingRuleEntry;
typedef struct backend			Slapi_Backend;
typedef struct _guid_t			Slapi_UniqueID;
typedef struct op			Slapi_Operation;
typedef struct conn			Slapi_Connection;
typedef struct slapi_dn			Slapi_DN;
typedef struct slapi_rdn		Slapi_RDN;
typedef struct slapi_mod		Slapi_Mod;
typedef struct slapi_mods		Slapi_Mods;
typedef struct slapi_componentid	Slapi_ComponentId;


/*
 * The default thread stacksize for nspr21 is 64k (except on IRIX!  It's 32k!).
 * For OSF, we require a larger stacksize as actual storage allocation is
 * higher i.e pointers are allocated 8 bytes but lower 4 bytes are used.
 * The value 0 means use the default stacksize.
 *
 * larger stacksize (256KB) is needed on IRIX due to its 4KB BUFSIZ.
 * (@ pthread IRIX porting -- 01/11/99)
 *
 * Don't know why HP was defined as follows up until DS6.1x. HP BUFSIZ is 1KB
	#elif ( defined( hpux ))
	#define SLAPD_DEFAULT_THREAD_STACKSIZE  262144L
 */
#if ( defined( irix ))
#define SLAPD_DEFAULT_THREAD_STACKSIZE  262144L
#elif ( defined ( OSF1 ))
#define SLAPD_DEFAULT_THREAD_STACKSIZE  262144L
#elif ( defined ( AIX ))
#define SLAPD_DEFAULT_THREAD_STACKSIZE  262144L
/* All 64-bit builds get a bigger stack size */
#elif ( defined ( __LP64__ )) || defined (_LP64)
#define SLAPD_DEFAULT_THREAD_STACKSIZE  262144L
#else
#define SLAPD_DEFAULT_THREAD_STACKSIZE  0
#endif

/*
 * parameter block routines
 */
Slapi_PBlock *slapi_pblock_new( void ); /* allocate and initialize */
int slapi_pblock_get( Slapi_PBlock *pb, int arg, void *value );
int slapi_pblock_set( Slapi_PBlock *pb, int arg, void *value );
void slapi_pblock_destroy( Slapi_PBlock *pb );


/*
 * entry routines
 */
Slapi_Entry *slapi_str2entry( char *s, int flags );
/* Flags for slapi_str2entry() */
/* Remove duplicate values */
#define SLAPI_STR2ENTRY_REMOVEDUPVALS		1
/* Add any missing values from RDN */
#define SLAPI_STR2ENTRY_ADDRDNVALS		2
/* Provide a hint that the entry is large; this enables some optimizations
   related to large entries. */
#define SLAPI_STR2ENTRY_BIGENTRY		4
/* Check to see if the entry is a tombstone; if so, set the tombstone flag
   (SLAPI_ENTRY_FLAG_TOMBSTONE) */
#define SLAPI_STR2ENTRY_TOMBSTONE_CHECK		8
/* Ignore entry state information if present */
#define SLAPI_STR2ENTRY_IGNORE_STATE		16
/* Return entries that have a "version: 1" line as part of the LDIF
   representation */
#define SLAPI_STR2ENTRY_INCLUDE_VERSION_STR	32
/* Add any missing ancestor values based on the object class hierarchy */
#define SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES	64
/* Inform slapi_str2entry() that the LDIF input is not well formed.
   Well formed LDIF has no duplicate attribute values, already
   has the RDN as an attribute of the entry, and has all values for a
   given attribute type listed contiguously. */
#define SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF 128

char *slapi_entry2str_with_options( Slapi_Entry *e, int *len, int options );
/* Options for slapi_entry2str_with_options() */
#define SLAPI_DUMP_STATEINFO		1	/* replication state */
#define SLAPI_DUMP_UNIQUEID		2	/* unique ID */
#define SLAPI_DUMP_NOOPATTRS		4	/* suppress operational attrs */
#define SLAPI_DUMP_NOWRAP		8	/* no line breaks */
#define SLAPI_DUMP_MINIMAL_ENCODING	16	/* use less base64 encoding */

char *slapi_entry2str( Slapi_Entry *e, int *len );
Slapi_Entry *slapi_entry_alloc(void);
void slapi_entry_init(Slapi_Entry *e, char *dn, Slapi_Attr *a);
void slapi_entry_free( Slapi_Entry *e );
Slapi_Entry *slapi_entry_dup( const Slapi_Entry *e );
char *slapi_entry_get_dn( Slapi_Entry *e );
char *slapi_entry_get_ndn( Slapi_Entry *e );
const Slapi_DN *slapi_entry_get_sdn_const( const Slapi_Entry *e );
Slapi_DN *slapi_entry_get_sdn( Slapi_Entry *e );
const char *slapi_entry_get_dn_const( const Slapi_Entry *e );
void slapi_entry_set_dn( Slapi_Entry *e, char *dn );
void slapi_entry_set_sdn( Slapi_Entry *e, const Slapi_DN *sdn );
int slapi_entry_attr_find( const Slapi_Entry *e, const char *type, Slapi_Attr **attr );
int slapi_entry_first_attr( const Slapi_Entry *e, Slapi_Attr **attr );
int slapi_entry_next_attr( const Slapi_Entry *e, Slapi_Attr *prevattr, Slapi_Attr **attr );
const char *slapi_entry_get_uniqueid( const Slapi_Entry *e );
void slapi_entry_set_uniqueid( Slapi_Entry *e, char *uniqueid );
int slapi_entry_schema_check( Slapi_PBlock *pb, Slapi_Entry *e );
int slapi_entry_rdn_values_present( const Slapi_Entry *e );
int slapi_entry_add_rdn_values( Slapi_Entry *e );
int slapi_entry_attr_delete( Slapi_Entry *e, const char *type );
	char **slapi_entry_attr_get_charray(const Slapi_Entry* e, const char *type);
char *slapi_entry_attr_get_charptr(const Slapi_Entry* e, const char *type);
int slapi_entry_attr_get_int(const Slapi_Entry* e, const char *type);
unsigned int slapi_entry_attr_get_uint(const Slapi_Entry* e, const char *type);
long slapi_entry_attr_get_long( const Slapi_Entry* e, const char *type);
unsigned long slapi_entry_attr_get_ulong( const Slapi_Entry* e, const char *type);
PRBool slapi_entry_attr_get_bool( const Slapi_Entry* e, const char *type);
void slapi_entry_attr_set_charptr(Slapi_Entry* e, const char *type, const char *value);
void slapi_entry_attr_set_int( Slapi_Entry* e, const char *type, int l);
void slapi_entry_attr_set_uint( Slapi_Entry* e, const char *type, unsigned int l);
void slapi_entry_attr_set_long(Slapi_Entry* e, const char *type, long l);
void slapi_entry_attr_set_ulong(Slapi_Entry* e, const char *type, unsigned long l);
int slapi_entry_attr_has_syntax_value(const Slapi_Entry *e, const char *type, const Slapi_Value *value);
int slapi_entry_has_children(const Slapi_Entry *e);
int slapi_is_rootdse( const char *dn );
size_t slapi_entry_size(Slapi_Entry *e);
int slapi_entry_attr_merge_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals );
int slapi_entry_add_values_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals );
int slapi_entry_add_valueset(Slapi_Entry *e, const char *type, Slapi_ValueSet *vs);
int slapi_entry_delete_values_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals );
int slapi_entry_merge_values_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals );
int slapi_entry_attr_replace_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals );
int slapi_entry_add_value(Slapi_Entry *e, const char *type, const Slapi_Value *value);
int slapi_entry_add_string(Slapi_Entry *e, const char *type, const char *value);
int slapi_entry_delete_string(Slapi_Entry *e, const char *type, const char *value);
void slapi_entry_diff(Slapi_Mods *smods, Slapi_Entry *e1, Slapi_Entry *e2, int diff_ctrl);


/*
 * Entry flags.
 */
#define SLAPI_ENTRY_FLAG_TOMBSTONE		1
int slapi_entry_flag_is_set( const Slapi_Entry *e, unsigned char flag );
void slapi_entry_set_flag( Slapi_Entry *e, unsigned char flag);
void slapi_entry_clear_flag( Slapi_Entry *e, unsigned char flag);

/* exported vattrcache routines */

int slapi_entry_vattrcache_watermark_isvalid(const Slapi_Entry *e);
void slapi_entry_vattrcache_watermark_set(Slapi_Entry *e);
void slapi_entry_vattrcache_watermark_invalidate(Slapi_Entry *e);
void slapi_entrycache_vattrcache_watermark_invalidate();




/*
 * Slapi_DN routines
 */
Slapi_DN *slapi_sdn_new( void );
Slapi_DN *slapi_sdn_new_dn_byval(const char *dn);
Slapi_DN *slapi_sdn_new_ndn_byval(const char *ndn);
Slapi_DN *slapi_sdn_new_dn_byref(const char *dn);
Slapi_DN *slapi_sdn_new_ndn_byref(const char *ndn);
Slapi_DN *slapi_sdn_new_dn_passin(const char *dn);
Slapi_DN *slapi_sdn_set_dn_byval(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_set_dn_byref(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_set_dn_passin(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_set_ndn_byval(Slapi_DN *sdn, const char *ndn);
Slapi_DN *slapi_sdn_set_ndn_byref(Slapi_DN *sdn, const char *ndn);
void slapi_sdn_done(Slapi_DN *sdn);
void slapi_sdn_free(Slapi_DN **sdn);
const char * slapi_sdn_get_dn(const Slapi_DN *sdn);
const char * slapi_sdn_get_ndn(const Slapi_DN *sdn);
void slapi_sdn_get_parent(const Slapi_DN *sdn,Slapi_DN *sdn_parent);
void slapi_sdn_get_backend_parent(const Slapi_DN *sdn,Slapi_DN *sdn_parent,const Slapi_Backend *backend);
Slapi_DN * slapi_sdn_dup(const Slapi_DN *sdn);
void slapi_sdn_copy(const Slapi_DN *from, Slapi_DN *to);
int slapi_sdn_compare( const Slapi_DN *sdn1, const Slapi_DN *sdn2 );
int slapi_sdn_isempty( const Slapi_DN *sdn);
int slapi_sdn_issuffix(const Slapi_DN *sdn, const Slapi_DN *suffixsdn);
int slapi_sdn_isparent( const Slapi_DN *parent, const Slapi_DN *child );
int slapi_sdn_isgrandparent( const Slapi_DN *parent, const Slapi_DN *child );
int slapi_sdn_get_ndn_len(const Slapi_DN *sdn);
int slapi_sdn_scope_test( const Slapi_DN *dn, const Slapi_DN *base, int scope );
void slapi_sdn_get_rdn(const Slapi_DN *sdn,Slapi_RDN *rdn);
Slapi_DN *slapi_sdn_set_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn);
Slapi_DN *slapi_sdn_set_parent(Slapi_DN *sdn, const Slapi_DN *parentdn);
int slapi_sdn_is_rdn_component(const Slapi_DN *rdn, const Slapi_Attr *a, const Slapi_Value *v);
char * slapi_moddn_get_newdn(Slapi_DN *dn_olddn, char *newrdn, char *newsuperiordn);


/*
 * Slapi_RDN functions
 */
Slapi_RDN *slapi_rdn_new( void );
Slapi_RDN *slapi_rdn_new_dn(const char *dn);
Slapi_RDN *slapi_rdn_new_sdn(const Slapi_DN *sdn);
Slapi_RDN *slapi_rdn_new_rdn(const Slapi_RDN *fromrdn);
void slapi_rdn_init(Slapi_RDN *rdn);
void slapi_rdn_init_dn(Slapi_RDN *rdn,const char *dn);
void slapi_rdn_init_sdn(Slapi_RDN *rdn,const Slapi_DN *sdn);
void slapi_rdn_init_rdn(Slapi_RDN *rdn,const Slapi_RDN *fromrdn);
void slapi_rdn_set_dn(Slapi_RDN *rdn,const char *dn);
void slapi_rdn_set_sdn(Slapi_RDN *rdn,const Slapi_DN *sdn);
void slapi_rdn_set_rdn(Slapi_RDN *rdn,const Slapi_RDN *fromrdn);
void slapi_rdn_free(Slapi_RDN **rdn);
void slapi_rdn_done(Slapi_RDN *rdn);
int slapi_rdn_get_first(Slapi_RDN *rdn, char **type, char **value);
int slapi_rdn_get_next(Slapi_RDN *rdn, int index, char **type, char **value);
int slapi_rdn_get_index(Slapi_RDN *rdn, const char *type, const char *value,size_t length);
int slapi_rdn_get_index_attr(Slapi_RDN *rdn, const char *type, char **value);
int slapi_rdn_contains(Slapi_RDN *rdn, const char *type, const char *value,size_t length);
int slapi_rdn_contains_attr(Slapi_RDN *rdn, const char *type, char **value);
int slapi_rdn_add(Slapi_RDN *rdn, const char *type, const char *value);
int slapi_rdn_remove_index(Slapi_RDN *rdn, int atindex);
int slapi_rdn_remove(Slapi_RDN *rdn, const char *type, const char *value, size_t length);
int slapi_rdn_remove_attr(Slapi_RDN *rdn, const char *type);
int slapi_rdn_isempty(const Slapi_RDN *rdn);
int slapi_rdn_get_num_components(Slapi_RDN *rdn);
int slapi_rdn_compare(Slapi_RDN *rdn1, Slapi_RDN *rdn2);
const char *slapi_rdn_get_rdn(const Slapi_RDN *rdn);
const char *slapi_rdn_get_nrdn(const Slapi_RDN *rdn); 
Slapi_DN *slapi_sdn_add_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn);


/*
 * utility routines for dealing with DNs
 */
char *slapi_dn_normalize( char *dn );
char *slapi_dn_normalize_to_end( char *dn, char *end );
char *slapi_dn_ignore_case( char *dn );
char *slapi_dn_normalize_case( char *dn );
char *slapi_dn_beparent( Slapi_PBlock *pb, const char *dn );
const char *slapi_dn_find_parent( const char *dn );
char *slapi_dn_parent( const char *dn );
int slapi_dn_issuffix( const char *dn, const char *suffix );
int slapi_dn_isparent( const char *parentdn, const char *childdn );
int slapi_dn_isroot( const char *dn );
int slapi_dn_isbesuffix( Slapi_PBlock *pb, const char *dn );
int slapi_rdn2typeval( char *rdn, char **type, struct berval *bv );
char *slapi_dn_plus_rdn(const char *dn, const char *rdn);


/*
 * thread safe random functions
 */
int slapi_rand_r(unsigned int * seed);
void slapi_rand_array(void *randx, size_t len);
int slapi_rand();


/*
 * attribute routines
 */
Slapi_Attr *slapi_attr_new( void );
Slapi_Attr *slapi_attr_init(Slapi_Attr *a, const char *type);
void slapi_attr_free( Slapi_Attr **a );
Slapi_Attr *slapi_attr_dup(const Slapi_Attr *attr);
int slapi_attr_add_value(Slapi_Attr *a, const Slapi_Value *v);
int slapi_attr_type2plugin( const char *type, void **pi );
int slapi_attr_get_type( Slapi_Attr *attr, char **type );
int slapi_attr_get_oid_copy( const Slapi_Attr *attr, char **oidp );
int slapi_attr_get_flags( const Slapi_Attr *attr, unsigned long *flags );
int slapi_attr_flag_is_set( const Slapi_Attr *attr, unsigned long flag );
int slapi_attr_value_cmp( const Slapi_Attr *attr, const struct berval *v1, const struct berval *v2 );
int slapi_attr_value_find( const Slapi_Attr *a, const struct berval *v );

int slapi_attr_type_cmp( const char *t1, const char *t2, int opt );
/* Mode of operation (opt) values for slapi_attr_type_cmp() */
#define SLAPI_TYPE_CMP_EXACT	0
#define SLAPI_TYPE_CMP_BASE	1
#define SLAPI_TYPE_CMP_SUBTYPE	2

int slapi_attr_types_equivalent(const char *t1, const char *t2);
char *slapi_attr_basetype( const char *type, char *buf, size_t bufsiz );
int slapi_attr_first_value( Slapi_Attr *a, Slapi_Value **v );
int slapi_attr_next_value( Slapi_Attr *a, int hint, Slapi_Value **v );
int slapi_attr_get_numvalues( const Slapi_Attr *a, int *numValues);
int slapi_attr_get_valueset(const Slapi_Attr *a, Slapi_ValueSet **vs);
/* Make the valuset in Slapi_Attr be *vs--not a copy */
int slapi_attr_set_valueset(Slapi_Attr *a, const Slapi_ValueSet *vs);
int slapi_attr_get_bervals_copy( Slapi_Attr *a, struct berval ***vals );
char * slapi_attr_syntax_normalize( const char *s );
void slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2);
void slapi_valueset_set_from_smod(Slapi_ValueSet *vs, Slapi_Mod *smod);


/*
 * value routines
 */
Slapi_Value *slapi_value_new( void );
Slapi_Value *slapi_value_new_berval(const struct berval *bval);
Slapi_Value *slapi_value_new_value(const Slapi_Value *v);
Slapi_Value *slapi_value_new_string(const char *s);
Slapi_Value *slapi_value_new_string_passin(char *s);
Slapi_Value *slapi_value_init(Slapi_Value *v);
Slapi_Value *slapi_value_init_berval(Slapi_Value *v, struct berval *bval);
Slapi_Value *slapi_value_init_string(Slapi_Value *v,const char *s);
Slapi_Value *slapi_value_init_string_passin(Slapi_Value *v, char *s);
Slapi_Value *slapi_value_dup(const Slapi_Value *v);
void slapi_value_free(Slapi_Value **value);
const struct berval *slapi_value_get_berval( const Slapi_Value *value );
Slapi_Value *slapi_value_set_berval( Slapi_Value *value, const struct berval *bval );
Slapi_Value *slapi_value_set_value( Slapi_Value *value, const Slapi_Value *vfrom);
Slapi_Value *slapi_value_set( Slapi_Value *value, void *val, unsigned long len);
int slapi_value_set_string(Slapi_Value *value, const char *strVal);
int slapi_value_set_string_passin(Slapi_Value *value, char *strVal);
int slapi_value_set_int(Slapi_Value *value, int intVal);
const char*slapi_value_get_string(const Slapi_Value *value);
int slapi_value_get_int(const Slapi_Value *value);
unsigned int slapi_value_get_uint(const Slapi_Value *value);
long slapi_value_get_long(const Slapi_Value *value);
unsigned long slapi_value_get_ulong(const Slapi_Value *value);
size_t slapi_value_get_length(const Slapi_Value *value);
int slapi_value_compare(const Slapi_Attr *a,const Slapi_Value *v1,const Slapi_Value *v2);


/*
 * Valueset functions.
 */
#define SLAPI_VALUE_FLAG_PASSIN			0x1
#define SLAPI_VALUE_FLAG_IGNOREERROR	0x2
#define SLAPI_VALUE_FLAG_PRESERVECSNSET	0x4
Slapi_ValueSet *slapi_valueset_new( void );
void slapi_valueset_free(Slapi_ValueSet *vs);
void slapi_valueset_init(Slapi_ValueSet *vs);
void slapi_valueset_done(Slapi_ValueSet *vs);
void slapi_valueset_add_value(Slapi_ValueSet *vs, const Slapi_Value *addval);
void slapi_valueset_add_value_ext(Slapi_ValueSet *vs, Slapi_Value *addval, unsigned long flags);
int slapi_valueset_first_value( Slapi_ValueSet *vs, Slapi_Value **v );
int slapi_valueset_next_value( Slapi_ValueSet *vs, int index, Slapi_Value **v);
int slapi_valueset_count( const Slapi_ValueSet *vs);
void slapi_valueset_set_from_smod(Slapi_ValueSet *vs, Slapi_Mod *smod);
void slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2);
Slapi_Value *slapi_valueset_find(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v);


/*
 * operation routines
 */
int slapi_op_abandoned( Slapi_PBlock *pb );
unsigned long slapi_op_get_type(Slapi_Operation * op);
void slapi_operation_set_flag(Slapi_Operation *op, unsigned long flag);
void slapi_operation_clear_flag(Slapi_Operation *op, unsigned long flag);
int slapi_operation_is_flag_set(Slapi_Operation *op, unsigned long flag);
int slapi_op_reserved(Slapi_PBlock *pb);
void slapi_operation_set_csngen_handler ( Slapi_Operation *op, void *callback );
void slapi_operation_set_replica_attr_handler ( Slapi_Operation *op, void *callback );
int slapi_operation_get_replica_attr ( Slapi_PBlock *pb, Slapi_Operation *op, const char *type, void *value );
char *slapi_op_type_to_string(unsigned long type);

/*
 * LDAPMod manipulation routines
 */
Slapi_Mods* slapi_mods_new( void );
void slapi_mods_init(Slapi_Mods *smods, int initCount);
void slapi_mods_init_byref(Slapi_Mods *smods, LDAPMod **mods);
void slapi_mods_init_passin(Slapi_Mods *smods, LDAPMod **mods);
void slapi_mods_free(Slapi_Mods **smods);
void slapi_mods_done(Slapi_Mods *smods);
void slapi_mods_insert_at(Slapi_Mods *smods, LDAPMod *mod, int pos);
void slapi_mods_insert_smod_at(Slapi_Mods *smods, Slapi_Mod *smod, int pos);
void slapi_mods_insert_before(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_insert_smod_before(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_insert_after(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_insert_after(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_add( Slapi_Mods *smods, int modtype, const char *type, unsigned long len, const char *val);
void slapi_mods_add_ldapmod(Slapi_Mods *smods, LDAPMod *mod);
void slapi_mods_add_modbvps( Slapi_Mods *smods, int modtype, const char *type, struct berval **bvps );
void slapi_mods_add_mod_values( Slapi_Mods *smods, int modtype, const char *type, Slapi_Value **va );
void slapi_mods_add_smod(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_add_string( Slapi_Mods *smods, int modtype, const char *type, const char *val);
void slapi_mods_remove(Slapi_Mods *smods);
LDAPMod *slapi_mods_get_first_mod(Slapi_Mods *smods);
LDAPMod *slapi_mods_get_next_mod(Slapi_Mods *smods);
Slapi_Mod *slapi_mods_get_first_smod(Slapi_Mods *smods, Slapi_Mod *smod);
Slapi_Mod *slapi_mods_get_next_smod(Slapi_Mods *smods, Slapi_Mod *smod);
void slapi_mods_iterator_backone(Slapi_Mods *smods);
LDAPMod **slapi_mods_get_ldapmods_byref(Slapi_Mods *smods);
LDAPMod **slapi_mods_get_ldapmods_passout(Slapi_Mods *smods);
int slapi_mods_get_num_mods(const Slapi_Mods *smods);
void slapi_mods_dump(const Slapi_Mods *smods, const char *text);

Slapi_Mod* slapi_mod_new( void );
void slapi_mod_init(Slapi_Mod *smod, int initCount);
void slapi_mod_init_byval(Slapi_Mod *smod, const LDAPMod *mod);
void slapi_mod_init_byref(Slapi_Mod *smod, LDAPMod *mod);
void slapi_mod_init_passin(Slapi_Mod *smod, LDAPMod *mod);
void slapi_mod_add_value(Slapi_Mod *smod, const struct berval *val);
void slapi_mod_remove_value(Slapi_Mod *smod);
struct berval *slapi_mod_get_first_value(Slapi_Mod *smod);
struct berval *slapi_mod_get_next_value(Slapi_Mod *smod);
const char *slapi_mod_get_type(const Slapi_Mod *smod);
int slapi_mod_get_operation(const Slapi_Mod *smod);
void slapi_mod_set_type(Slapi_Mod *smod, const char *type);
void slapi_mod_set_operation(Slapi_Mod *smod, int op);
int slapi_mod_get_num_values(const Slapi_Mod *smod);
const LDAPMod *slapi_mod_get_ldapmod_byref(const Slapi_Mod *smod);
LDAPMod *slapi_mod_get_ldapmod_passout(Slapi_Mod *smod);
void slapi_mod_free(Slapi_Mod **smod);
void slapi_mod_done(Slapi_Mod *mod);
int slapi_mod_isvalid(const Slapi_Mod *mod);
void slapi_mod_dump(LDAPMod *mod, int n);


/* helper functions to translate between entry and a set of mods */
int slapi_mods2entry(Slapi_Entry **e, const char *dn, LDAPMod **attrs);
int slapi_entry2mods(const Slapi_Entry *e, char **dn, LDAPMod ***attrs);


/*
 * routines for dealing with filters
 */
int slapi_filter_get_choice( Slapi_Filter *f );
int slapi_filter_get_ava( Slapi_Filter *f, char **type, struct berval **bval );
int slapi_filter_get_attribute_type( Slapi_Filter *f, char **type );
int slapi_filter_get_subfilt( Slapi_Filter *f, char **type, char **initial,
	char ***any, char **final );
Slapi_Filter *slapi_filter_list_first( Slapi_Filter *f );
Slapi_Filter *slapi_filter_list_next( Slapi_Filter *f, Slapi_Filter *fprev );
Slapi_Filter *slapi_str2filter( char *str );
Slapi_Filter *slapi_filter_join( int ftype, Slapi_Filter *f1,
	Slapi_Filter *f2 );
Slapi_Filter *slapi_filter_join_ex( int ftype, Slapi_Filter *f1, 
	Slapi_Filter *f2, int recurse_always );

void slapi_filter_free( Slapi_Filter *f, int recurse );
int slapi_filter_test( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f,
	int verify_access );
int slapi_vattr_filter_test( Slapi_PBlock *pb, Slapi_Entry *e,
								struct slapi_filter	*f, int verify_access);
int slapi_filter_test_simple( Slapi_Entry *e, Slapi_Filter *f);
char *slapi_find_matching_paren( const char *str );
int slapi_filter_test_ext( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f,
	int verify_access , int only_test_access);
int slapi_vattr_filter_test_ext( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Filter *f,
	int verify_access , int only_test_access);
int slapi_filter_compare(struct slapi_filter *f1, struct slapi_filter *f2);
Slapi_Filter *slapi_filter_dup(Slapi_Filter *f);
int slapi_filter_changetype(Slapi_Filter *f, const char *newtype);


/*
 * slapi_filter_apply() is used to apply a function to each simple filter
 * component within a complex filter.  A 'simple filter' is anything other
 * than AND, OR or NOT.
 */
typedef int (*FILTER_APPLY_FN)( Slapi_Filter *f, void *arg);
int slapi_filter_apply( struct slapi_filter *f, FILTER_APPLY_FN fn, void *arg,
	int *error_code );
/*
 * Possible return values for slapi_filter_apply() and FILTER_APPLY_FNs.
 * Note that a FILTER_APPLY_FN should return _STOP or _CONTINUE only.
 */
#define SLAPI_FILTER_SCAN_STOP		-1	/* premature abort */
#define SLAPI_FILTER_SCAN_ERROR		-2 	/* an error occurred */
#define SLAPI_FILTER_SCAN_NOMORE	0	/* success */
#define SLAPI_FILTER_SCAN_CONTINUE	1	/* continue scanning */
/* Error codes that slapi_filter_apply() may set in *error_code */
#define SLAPI_FILTER_UNKNOWN_FILTER_TYPE 2
	

/*
 * Bit-Twiddlers
 */
unsigned char slapi_setbit_uchar(unsigned char f,unsigned char bitnum);
unsigned char slapi_unsetbit_uchar(unsigned char f,unsigned char bitnum);
int slapi_isbitset_uchar(unsigned char f,unsigned char bitnum);
unsigned int slapi_setbit_int(unsigned int f,unsigned int bitnum);
unsigned int slapi_unsetbit_int(unsigned int f,unsigned int bitnum);
int slapi_isbitset_int(unsigned int f,unsigned int bitnum);


/*
 * routines for sending entries and results to the client
 */
int slapi_send_ldap_search_entry( Slapi_PBlock *pb, Slapi_Entry *e,
	LDAPControl **ectrls, char **attrs, int attrsonly );
void slapi_send_ldap_result( Slapi_PBlock *pb, int err, char *matched,
	char *text, int nentries, struct berval **urls );
int slapi_send_ldap_referral( Slapi_PBlock *pb, Slapi_Entry *e,
	struct berval **refs, struct berval ***urls );
typedef int (*send_ldap_search_entry_fn_ptr_t)( Slapi_PBlock *pb,
	Slapi_Entry *e, LDAPControl **ectrls, char **attrs, int attrsonly );
typedef void (*send_ldap_result_fn_ptr_t)( Slapi_PBlock *pb, int err,
	char *matched, char *text, int nentries, struct berval **urls );
typedef int (*send_ldap_referral_fn_ptr_t)( Slapi_PBlock *pb,
	Slapi_Entry *e, struct berval **refs, struct berval ***urls );


/*
 * matching rule
 */
typedef int (*mrFilterMatchFn) (void* filter, Slapi_Entry*, Slapi_Attr* vals);
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
int slapi_mr_indexer_create(Slapi_PBlock* opb);
int slapi_mr_filter_index(Slapi_Filter* f, Slapi_PBlock* pb);
int slapi_berval_cmp(const struct berval* L, const struct berval* R);
#define SLAPI_BERVAL_EQ(L,R) ((L)->bv_len == (R)->bv_len && \
        ! memcmp ((L)->bv_val, (R)->bv_val, (L)->bv_len))

Slapi_MatchingRuleEntry *slapi_matchingrule_new(void);
void slapi_matchingrule_free(Slapi_MatchingRuleEntry **mrEntry,
                             int freeMembers);
int slapi_matchingrule_get(Slapi_MatchingRuleEntry *mr, int arg, void *value);
int slapi_matchingrule_set(Slapi_MatchingRuleEntry *mr, int arg, void *value);
int slapi_matchingrule_register(Slapi_MatchingRuleEntry *mrEntry);
int slapi_matchingrule_unregister(char *oid);

/*
 * access control
 */
int slapi_access_allowed( Slapi_PBlock *pb, Slapi_Entry *e, char *attr,
	struct berval *val, int access );
int slapi_acl_check_mods( Slapi_PBlock *pb, Slapi_Entry *e,
	LDAPMod **mods, char **errbuf );
int slapi_acl_verify_aci_syntax(Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf);


/*
 * attribute stuff
 */
int slapi_value_find( void *plugin, struct berval **vals, struct berval *v );


/*
 * password handling
 */
#define SLAPI_USERPWD_ATTR "userpassword"
int slapi_pw_find_sv( Slapi_Value **vals, const Slapi_Value *v );

/* value encoding encoding */
/* checks if the value is encoded with any known algorithm*/
int slapi_is_encoded(char *value); 
/* encode value with the specified algorithm */
char* slapi_encode(char *value, char *alg);


/* UTF8 related */
int slapi_has8thBit(unsigned char *s);
unsigned char *slapi_utf8StrToLower(unsigned char *s);
void slapi_utf8ToLower(unsigned char *s, unsigned char *d, int *ssz, int *dsz);
int slapi_utf8isUpper(unsigned char *s);
unsigned char *slapi_utf8StrToUpper(unsigned char *s);
void slapi_utf8ToUpper(unsigned char *s, unsigned char *d, int *ssz, int *dsz);
int slapi_utf8isLower(unsigned char *s);
int slapi_utf8casecmp(unsigned char *s0, unsigned char *s1);
int slapi_utf8ncasecmp(unsigned char *s0, unsigned char *s1, int n);

unsigned char *slapi_UTF8STRTOLOWER(char *s);
void slapi_UTF8TOLOWER(char *s, char *d, int *ssz, int *dsz);
int slapi_UTF8ISUPPER(char *s);
unsigned char *slapi_UTF8STRTOUPPER(char *s);
void slapi_UTF8TOUPPER(char *s, char *d, int *ssz, int *dsz);
int slapi_UTF8ISLOWER(char *s);
int slapi_UTF8CASECMP(char *s0, char *s1);
int slapi_UTF8NCASECMP(char *s0, char *s1, int n);



/*
 * Interface to the API broker service
 *
 * The API broker allows plugins to publish an API that may be discovered
 * and used dynamically at run time by other subsystems e.g. other plugins.
 */
										  
/* Function:	slapi_apib_register
   Description:	this function allows publication of an interface
   Parameters:	guid - a string constant that uniquely identifies the
		    interface (must exist for the life of the server)
		api - a vtable for the published api (must exist for the
		    life of the server or until the reference count,
		    if it exists, reaches zero)
   Return:	0 if function succeeds
		non-zero otherwise
*/
int slapi_apib_register(char *guid, void **api); /* publish an interface */

/* Function:	slapi_apib_unregister
   Description:	this function allows removal of a published interface
   Parameters:	guid - a string constant that uniquely identifies the interface
   Return:	0 if function succeeds
		non-zero otherwise
*/
int slapi_apib_unregister(char *guid); /* remove interface from published list */


/* Function:	slapi_apib_get_interface
   Description:	this function allows retrieval of a published interface,
		    if the api reference counted, then the reference
		    count is incremented
   Parameters:	guid - a string constant that uniquely identifies the
		    interface requested
		api - the retrieved vtable for the published api (must NOT
		    be freed)
   Return:	0 if function succeeds
		non-zero otherwise
*/
int slapi_apib_get_interface(char *guid, void ***api); /* retrieve an interface for use */


/* Function:	slapi_apib_make_reference_counted
   Description:	this function makes an interface a reference counted interface
		    it must be called prior to registering the interface
   Parameters:	api - the api to make a reference counted api
		callback - if non-zero, this must be a pointer to a function
		    which the api broker will call when the ref count for
		    the api reaches zero.  This function must return 0 if
		    it deregisters the api, non-zero otherwise
		api - the retrieved vtable for the published api (must NOT
		    be freed)
   Return:	0 if function succeeds
		non-zero otherwise
*/
typedef int (*slapi_apib_callback_on_zero)(void **api);

int slapi_apib_make_reference_counted(void **api,
	slapi_apib_callback_on_zero callback);


/* Function:	slapi_apib_addref
   Description:	this function adds to the reference count of an api - a
		    call to this function should be paired with a call
		    to slapi_apib_release
		 - ONLY FOR REFERENCE COUNTED APIS
   Parameters:	api - the api to add a reference to
   Return:	the new reference count
*/
int slapi_apib_addref(void **api);


/* Function:	slapi_apib_release
   Description:	this function adds to the reference count of an api - a
		    call to this function should be paired with a prior call
		    to slapi_apib_addref or slapi_apib_get_interface
		- ONLY FOR REFERENCE COUNTED APIS
   Parameters:	api - the api to add a reference to
   Return:	the new reference count
*/
int slapi_apib_release(void **api);

/**** End of API broker interface. *******************************************/


/*
 * routines for dealing with controls
 */
int slapi_control_present( LDAPControl **controls, char *oid,
	struct berval **val, int *iscritical );
void slapi_register_supported_control( char *controloid,
	unsigned long controlops );
LDAPControl * slapi_dup_control( LDAPControl *ctrl );

#define SLAPI_OPERATION_BIND		0x00000001UL
#define SLAPI_OPERATION_UNBIND		0x00000002UL
#define SLAPI_OPERATION_SEARCH		0x00000004UL
#define SLAPI_OPERATION_MODIFY		0x00000008UL
#define SLAPI_OPERATION_ADD		0x00000010UL
#define SLAPI_OPERATION_DELETE		0x00000020UL
#define SLAPI_OPERATION_MODDN		0x00000040UL
#define SLAPI_OPERATION_MODRDN		SLAPI_OPERATION_MODDN
#define SLAPI_OPERATION_COMPARE		0x00000080UL
#define SLAPI_OPERATION_ABANDON		0x00000100UL
#define SLAPI_OPERATION_EXTENDED	0x00000200UL
#define SLAPI_OPERATION_ANY		0xFFFFFFFFUL
#define SLAPI_OPERATION_NONE		0x00000000UL
int slapi_get_supported_controls_copy( char ***ctrloidsp,
	unsigned long **ctrlopsp );
int slapi_build_control( char *oid, BerElement *ber,
        char iscritical, LDAPControl **ctrlp );
int slapi_build_control_from_berval( char *oid, struct berval *bvp,
        char iscritical, LDAPControl **ctrlp );


/*
 * routines for dealing with extended operations
 */
char **slapi_get_supported_extended_ops_copy( void );


/*
 * bind, including SASL 
 */
void slapi_register_supported_saslmechanism( char *mechanism );
char ** slapi_get_supported_saslmechanisms_copy( void );
void slapi_add_auth_response_control( Slapi_PBlock *pb, const char *binddn );
int slapi_add_pwd_control( Slapi_PBlock *pb, char *arg, long time );
int slapi_pwpolicy_make_response_control (Slapi_PBlock *pb, int seconds, int logins, int error);
/* Password Policy Response Control stuff - the error argument above */
#define LDAP_PWPOLICY_PWDEXPIRED		0
#define LDAP_PWPOLICY_ACCTLOCKED		1
#define LDAP_PWPOLICY_CHGAFTERRESET		2
#define LDAP_PWPOLICY_PWDMODNOTALLOWED		3
#define LDAP_PWPOLICY_MUSTSUPPLYOLDPWD		4
#define LDAP_PWPOLICY_INVALIDPWDSYNTAX		5
#define LDAP_PWPOLICY_PWDTOOSHORT		6
#define LDAP_PWPOLICY_PWDTOOYOUNG		7
#define LDAP_PWPOLICY_PWDINHISTORY		8

/*
 * routine for freeing the ch_arrays returned by the slapi_get*_copy functions above
 */
void slapi_ch_array_free( char **array );


/*
 * checking routines for allocating and freeing memory
 */
char * slapi_ch_malloc( unsigned long size );
char * slapi_ch_realloc( char *block, unsigned long size );
char * slapi_ch_calloc( unsigned long nelem, unsigned long size );
char * slapi_ch_strdup( const char *s );
void slapi_ch_free( void **ptr );
void slapi_ch_free_string( char **s );
struct berval*  slapi_ch_bvdup(const struct berval*);
struct berval** slapi_ch_bvecdup(struct berval**);
void slapi_ch_bvfree(struct berval** v);
char * slapi_ch_smprintf(const char *fmt, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 1, 2)));
#else
        ;
#endif

/*
 * syntax plugin routines
 */
int slapi_call_syntax_values2keys_sv( void *vpi, Slapi_Value **vals,
	Slapi_Value ***ivals, int ftype );
int slapi_call_syntax_assertion2keys_ava_sv( void *vpi, Slapi_Value *val,
	Slapi_Value ***ivals, int ftype );
int slapi_call_syntax_assertion2keys_sub_sv( void *vpi, char *initial,
	char **any, char *final, Slapi_Value ***ivals );


/*
 * internal operation and plugin callback routines
 */
typedef void (*plugin_result_callback)(int rc, void *callback_data);
typedef int (*plugin_referral_entry_callback)(char * referral, 
	void *callback_data);
typedef int (*plugin_search_entry_callback)(Slapi_Entry *e, 
	void *callback_data);
void slapi_free_search_results_internal(Slapi_PBlock *pb);


/*
 * The following functions can be used for internal operations based on DN
 * as well as on uniqueid. These functions should be used by all new plugins
 * and preferrably old plugins should be changed to use them to take
 * advantage of new plugin configuration capabilities and to use an
 * extensible interface.
 *
 * These functions return -1 if pb is NULL and 0 otherwise.
 * The SLAPI_PLUGIN_INTOP_RESULT pblock parameter should be checked to
 * check if the operation was successful. 
 *
 * Helper functions are provided to set up pblock parameters currently used
 * by the functions, e.g., slapi_search_internal_set_pb().
 * Additional parameters may be set directly in the pblock.
 */

int slapi_search_internal_pb(Slapi_PBlock *pb);
int slapi_search_internal_callback_pb(Slapi_PBlock *pb, void *callback_data,
	plugin_result_callback prc, plugin_search_entry_callback psec,
	plugin_referral_entry_callback prec);
int slapi_add_internal_pb(Slapi_PBlock *pb);
int slapi_modify_internal_pb(Slapi_PBlock *pb);
int slapi_modrdn_internal_pb(Slapi_PBlock *pb);
int slapi_delete_internal_pb(Slapi_PBlock *pb);


int slapi_seq_internal_callback_pb(Slapi_PBlock *pb, void *callback_data,
	plugin_result_callback res_callback,
	plugin_search_entry_callback srch_callback,
	plugin_referral_entry_callback ref_callback);

void slapi_search_internal_set_pb(Slapi_PBlock *pb, const char *base,
	int scope, const char *filter, char **attrs, int attrsonly,
	LDAPControl **controls, const char *uniqueid,
	Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_add_entry_internal_set_pb(Slapi_PBlock *pb, Slapi_Entry *e,
	LDAPControl **controls, Slapi_ComponentId *plugin_identity,
	int operation_flags);
int slapi_add_internal_set_pb(Slapi_PBlock *pb, const char *dn,
	LDAPMod **attrs, LDAPControl **controls,
	Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_modify_internal_set_pb(Slapi_PBlock *pb, const char *dn,
	LDAPMod **mods, LDAPControl **controls, const char *uniqueid,
	Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_rename_internal_set_pb(Slapi_PBlock *pb, const char *olddn,
	const char *newrdn, const char *newsuperior, int deloldrdn,
	LDAPControl **controls, const char *uniqueid,
	Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_delete_internal_set_pb(Slapi_PBlock *pb, const char *dn,
	LDAPControl **controls, const char *uniqueid,
	Slapi_ComponentId *plugin_identity, int operation_flags);
void slapi_seq_internal_set_pb(Slapi_PBlock *pb, char *ibase, int type,
	char *attrname, char *val, char **attrs, int attrsonly,
	LDAPControl **controls, Slapi_ComponentId *plugin_identity,
	int operation_flags);

/*
 * slapi_search_internal_get_entry() finds an entry given a dn.  It returns
 * an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int slapi_search_internal_get_entry( Slapi_DN *dn, char ** attrlist,
	Slapi_Entry **ret_entry , void *caller_identity);

/* 
 * interface for registering object extensions.
 */
typedef void *(*slapi_extension_constructor_fnptr)(void *object, void *parent);

typedef void (*slapi_extension_destructor_fnptr)(void *extension,
	void *object, void *parent);

int slapi_register_object_extension( const char *pluginname,
	const char *objectname, slapi_extension_constructor_fnptr constructor, 
	slapi_extension_destructor_fnptr destructor, int *objecttype,
	int *extensionhandle);

/* objects that can be extended (possible values for the objectname param.) */
#define SLAPI_EXT_CONNECTION	"Connection"
#define SLAPI_EXT_OPERATION	"Operation"
#define SLAPI_EXT_ENTRY		"Entry"
#define SLAPI_EXT_MTNODE	"Mapping Tree Node"

void *slapi_get_object_extension(int objecttype, void *object,
	int extensionhandle);
void slapi_set_object_extension(int objecttype, void *object,
	int extensionhandle, void *extension);

/*
 * interface to allow a plugin to register additional plugins.
 */
typedef int (*slapi_plugin_init_fnptr)( Slapi_PBlock *pb );
int slapi_register_plugin( const char *plugintype, int enabled,
	const char *initsymbol, slapi_plugin_init_fnptr initfunc,
	const char *name, char **argv, void *group_identity);


/*
 * logging
 */
int slapi_log_error( int severity, char *subsystem, char *fmt, ... );
/* allowed values for the "severity" parameter */
#define SLAPI_LOG_FATAL          	0
#define SLAPI_LOG_TRACE			1
#define SLAPI_LOG_PACKETS		2
#define SLAPI_LOG_ARGS			3
#define SLAPI_LOG_CONNS			4
#define SLAPI_LOG_BER			5
#define SLAPI_LOG_FILTER		6
#define SLAPI_LOG_CONFIG		7
#define SLAPI_LOG_ACL			8
#define SLAPI_LOG_SHELL			9
#define SLAPI_LOG_PARSE			10
#define SLAPI_LOG_HOUSE			11
#define SLAPI_LOG_REPL			12
#define SLAPI_LOG_CACHE			13
#define SLAPI_LOG_PLUGIN		14
#define SLAPI_LOG_TIMING		15
#define SLAPI_LOG_ACLSUMMARY		16

int slapi_is_loglevel_set( const int loglevel );


/*
 * locks and synchronization
 */
typedef struct slapi_mutex	Slapi_Mutex;
typedef struct slapi_condvar	Slapi_CondVar;
Slapi_Mutex *slapi_new_mutex( void );
void slapi_destroy_mutex( Slapi_Mutex *mutex );
void slapi_lock_mutex( Slapi_Mutex *mutex );
int slapi_unlock_mutex( Slapi_Mutex *mutex );
Slapi_CondVar *slapi_new_condvar( Slapi_Mutex *mutex );
void slapi_destroy_condvar( Slapi_CondVar *cvar );
int slapi_wait_condvar( Slapi_CondVar *cvar, struct timeval *timeout );
int slapi_notify_condvar( Slapi_CondVar *cvar, int notify_all );


/*
 * thread-safe LDAP connections
 */
LDAP *slapi_ldap_init( char *ldaphost, int ldapport, int secure, int shared );
void slapi_ldap_unbind( LDAP *ld );


/*
 * computed attributes
 */
struct _computed_attr_context;
typedef struct _computed_attr_context computed_attr_context; 
typedef int (*slapi_compute_output_t)(computed_attr_context *c,Slapi_Attr *a , Slapi_Entry *e);
typedef int (*slapi_compute_callback_t)(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn);
typedef int (*slapi_search_rewrite_callback_t)(Slapi_PBlock *pb);
int slapi_compute_add_evaluator(slapi_compute_callback_t function);
int slapi_compute_add_search_rewriter(slapi_search_rewrite_callback_t function);
int	compute_rewrite_search_filter(Slapi_PBlock *pb);


/*
 * routines for dealing with backends
 */
Slapi_Backend *slapi_be_new( const char *type, const char *name,
	int isprivate, int logchanges );
void slapi_be_free(Slapi_Backend **be);
Slapi_Backend *slapi_be_select( const Slapi_DN *sdn );
Slapi_Backend *slapi_be_select_by_instance_name( const char *name );
int slapi_be_exist(const Slapi_DN *sdn);
void slapi_be_delete_onexit(Slapi_Backend *be);
void slapi_be_set_readonly(Slapi_Backend *be, int readonly);
int slapi_be_get_readonly(Slapi_Backend *be);
int slapi_be_getentrypoint(Slapi_Backend *be, int entrypoint, void **ret_fnptr,
                           Slapi_PBlock *pb);
int slapi_be_setentrypoint(Slapi_Backend *be, int entrypoint, void *ret_fnptr, 
			   Slapi_PBlock *pb);
int slapi_be_logchanges(Slapi_Backend *be);
int slapi_be_issuffix(const Slapi_Backend *be, const Slapi_DN *suffix );
void slapi_be_addsuffix(Slapi_Backend *be,const Slapi_DN *suffix);
char * slapi_be_get_name(Slapi_Backend * be);
const Slapi_DN *slapi_be_getsuffix(Slapi_Backend *be, int n);
Slapi_Backend* slapi_get_first_backend(char **cookie);
Slapi_Backend* slapi_get_next_backend(char *cookie);
int slapi_be_private( Slapi_Backend *be );
void * slapi_be_get_instance_info(Slapi_Backend * be);
void  slapi_be_set_instance_info(Slapi_Backend * be, void * data);
Slapi_DN * slapi_get_first_suffix(void ** node, int show_private);
Slapi_DN * slapi_get_next_suffix(void ** node, int show_private);
int slapi_is_root_suffix(Slapi_DN * dn);
const char * slapi_be_gettype(Slapi_Backend *be);

int slapi_be_is_flag_set(Slapi_Backend * be, int flag);
void slapi_be_set_flag(Slapi_Backend * be, int flag);
#define SLAPI_BE_FLAG_REMOTE_DATA   0x1  /* entries held by backend are remote */
#define SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST   0x10  /* force to call filter_test (search only) */


/* These functions allow a plugin to register for callback when
 * a backend state change
 */
typedef void (*slapi_backend_state_change_fnptr)(void *handle, char *be_name,
	 int old_be_state, int new_be_state);
void slapi_register_backend_state_change(void * handle, slapi_backend_state_change_fnptr funct);
int slapi_unregister_backend_state_change(void * handle);
#define	SLAPI_BE_STATE_ON       1	/* backend is ON */
#define	SLAPI_BE_STATE_OFFLINE 	2	/* backend is OFFLINE (import process) */
#define	SLAPI_BE_STATE_DELETE 	3	/* backend has been deleted */

/*
 * Distribution.
 */
/* SLAPI_BE_ALL_BACKENDS is a special value that is returned by
 * a distribution plugin function to indicate that all backends
 * should be searched (it is only used for search operations).
 */
#define SLAPI_BE_ALL_BACKENDS			-1



/*
 * virtual attribute service
 */

/* General flags (flags parameter) */
#define SLAPI_REALATTRS_ONLY						1
#define SLAPI_VIRTUALATTRS_ONLY						2
#define SLAPI_VIRTUALATTRS_REQUEST_POINTERS			4 /* I want to receive pointers into the entry, if possible */
#define  SLAPI_VIRTUALATTRS_LIST_OPERATIONAL_ATTRS	8 /* Include operational attributes in attribute lists */
#define SLAPI_VIRTUALATTRS_SUPPRESS_SUBTYPES		16 /* I want only the requested attribute */

/* Buffer disposition flags (buffer_flags parameter) */
#define SLAPI_VIRTUALATTRS_RETURNED_POINTERS	1
#define SLAPI_VIRTUALATTRS_RETURNED_COPIES	2
#define SLAPI_VIRTUALATTRS_REALATTRS_ONLY       4

/* Attribute type name disposition values (type_name_disposition parameter) */
#define SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS	1
#define SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE		2
#define SLAPI_VIRTUALATTRS_NOT_FOUND				-1
#define SLAPI_VIRTUALATTRS_LOOP_DETECTED			-2

typedef struct _vattr_type_thang vattr_type_thang;
typedef struct _vattr_get_thang vattr_get_thang;
vattr_get_thang *slapi_vattr_getthang_first(vattr_get_thang *t);
vattr_get_thang *slapi_vattr_getthang_next(vattr_get_thang *t);

int slapi_vattr_values_type_thang_get(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* attr type */ vattr_type_thang *type_thang,
	/* pointer to result set */ Slapi_ValueSet** results,
	int *type_name_disposition, char **actual_type_name, int flags,
	int *buffer_flags);
int slapi_vattr_values_get(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* attr type name */ char *type,
	/* pointer to result set */ Slapi_ValueSet** results,
	int *type_name_disposition, char **actual_type_name, int flags,
	int *buffer_flags);
int slapi_vattr_values_get_ex(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* attr type name */ char *type,
	/* pointer to result set */ Slapi_ValueSet*** results,
	int **type_name_disposition, char ***actual_type_name, int flags,
	int *buffer_flags, int *subtype_count);
int slapi_vattr_namespace_values_get(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* backend namespace dn */ Slapi_DN *namespace_dn,
	/* attr type name */ char *type,
	/* pointer to result set */ Slapi_ValueSet*** results,
	int **type_name_disposition, char ***actual_type_name, int flags,
	int *buffer_flags, int *subtype_count);
void slapi_vattr_values_free(Slapi_ValueSet **value, char **actual_type_name,
	int flags);
int slapi_vattr_value_compare(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* attr type name */ char *type,
	Slapi_Value *test_this,/* pointer to result */ int *result,
	int flags);
int slapi_vattr_namespace_value_compare(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* backend namespace dn */ Slapi_DN *namespace_dn,
	/* attr type name */ const char *type,
	Slapi_Value *test_this,/* pointer to result */ int *result,
	int flags);
int slapi_vattr_list_attrs(
	/* Entry we're interested in */ Slapi_Entry *e,
	/* pointer to receive the list */ vattr_type_thang **types,
	int flags, int *buffer_flags);
void slapi_vattr_attrs_free(vattr_type_thang **types, int flags);
char *vattr_typethang_get_name(vattr_type_thang *t);
unsigned long vattr_typethang_get_flags(vattr_type_thang *t);
vattr_type_thang *vattr_typethang_next(vattr_type_thang *t);
vattr_type_thang *vattr_typethang_first(vattr_type_thang *t);
int slapi_vattr_schema_check_type(Slapi_Entry *e, char *type);


/* roles */
typedef int (*roles_check_fn_type)(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present);

int slapi_role_check(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present);
void slapi_register_role_check(roles_check_fn_type check_fn);



/* Binder-based (connection centric) resource limits */
/*
 * Valid values for `type' parameter to slapi_reslimit_register().
 */		
#define SLAPI_RESLIMIT_TYPE_INT				0

/*
 * Status codes returned by all functions.
 */
#define SLAPI_RESLIMIT_STATUS_SUCCESS		0	/* goodness */
#define SLAPI_RESLIMIT_STATUS_NOVALUE		1	/* no value is available */
#define SLAPI_RESLIMIT_STATUS_INIT_FAILURE	2	/* initialization failed */
#define SLAPI_RESLIMIT_STATUS_PARAM_ERROR	3	/* bad parameter */
#define SLAPI_RESLIMIT_STATUS_UNKNOWN_HANDLE	4	/* unregistered handle */
#define SLAPI_RESLIMIT_STATUS_INTERNAL_ERROR	5	/* unexpected error */

/*
 * Functions.
 */
int slapi_reslimit_register( int type, const char *attrname, int *handlep );
int slapi_reslimit_get_integer_limit( Slapi_Connection *conn, int handle,
		int *limitp );
/* END of Binder-based resource limits API */



/*
 * Plugin and parameter block related macros (remainder of this file).
 */

/*
 * Plugin version.  Note that the Directory Server will load version 01
 * and 02 plugins, but some server features require 03 plugins.
 */
#define SLAPI_PLUGIN_VERSION_01		"01"
#define SLAPI_PLUGIN_VERSION_02		"02"
#define SLAPI_PLUGIN_VERSION_03         "03"
#define SLAPI_PLUGIN_CURRENT_VERSION	SLAPI_PLUGIN_VERSION_03
#define SLAPI_PLUGIN_IS_COMPAT(x)	\
	((strcmp((x), SLAPI_PLUGIN_VERSION_01) == 0) ||	\
	 (strcmp((x), SLAPI_PLUGIN_VERSION_02) == 0) || \
	 (strcmp((x), SLAPI_PLUGIN_VERSION_03) == 0))
#define SLAPI_PLUGIN_IS_V2(x)		\
	((strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_02) == 0) || \
         (strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_03) == 0))
#define SLAPI_PLUGIN_IS_V3(x)		\
	(strcmp((x)->plg_version, SLAPI_PLUGIN_VERSION_03) == 0)

/* this one just has to be human readable */
#define SLAPI_PLUGIN_SUPPORTED_VERSIONS	"01,02,03"

/*
 * types of plugin interfaces
 */
#define SLAPI_PLUGIN_EXTENDEDOP			2
#define SLAPI_PLUGIN_PREOPERATION		3
#define SLAPI_PLUGIN_POSTOPERATION		4
#define SLAPI_PLUGIN_MATCHINGRULE		5
#define SLAPI_PLUGIN_SYNTAX			6
#define SLAPI_PLUGIN_ACL			7
#define	SLAPI_PLUGIN_BEPREOPERATION		8
#define SLAPI_PLUGIN_BEPOSTOPERATION		9
#define SLAPI_PLUGIN_ENTRY             		10
#define SLAPI_PLUGIN_TYPE_OBJECT       		11
#define SLAPI_PLUGIN_INTERNAL_PREOPERATION	12
#define SLAPI_PLUGIN_INTERNAL_POSTOPERATION	13
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME		14
#define SLAPI_PLUGIN_VATTR_SP			15
#define SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME	16
#define SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE	17
#define SLAPI_PLUGIN_INDEX			18

/*
 * special return values for extended operation plugins (zero or positive
 *     return values should be LDAP error codes as defined in ldap.h)
 */
#define SLAPI_PLUGIN_EXTENDED_SENT_RESULT	-1
#define SLAPI_PLUGIN_EXTENDED_NOT_HANDLED	-2

/*
 * the following can be used as the second argument to the
 * slapi_pblock_get() and slapi_pblock_set() calls.
 */

/* backend, connection, operation */
#define SLAPI_BACKEND               130
#define SLAPI_CONNECTION            131
#define SLAPI_OPERATION             132
#define SLAPI_REQUESTOR_ISROOT      133
#define SLAPI_BE_TYPE               135
#define SLAPI_BE_READONLY           136
#define SLAPI_BE_LASTMOD            137
#define SLAPI_CONN_ID               139
#define SLAPI_BACKEND_COUNT         860

/* operation */
#define SLAPI_OPINITIATED_TIME			140
#define SLAPI_REQUESTOR_DN			141
#define SLAPI_OPERATION_PARAMETERS		138
#define SLAPI_OPERATION_TYPE			590
#define SLAPI_OPERATION_AUTHTYPE		741
#define SLAPI_OPERATION_ID			744
#define SLAPI_IS_REPLICATED_OPERATION		142
#define SLAPI_IS_MMR_REPLICATED_OPERATION	153
#define SLAPI_IS_LEGACY_REPLICATED_OPERATION	154

/* connection */
#define SLAPI_CONN_DN        			143
#define SLAPI_CONN_CLIENTNETADDR	850
#define SLAPI_CONN_SERVERNETADDR			851
#define SLAPI_CONN_IS_REPLICATION_SESSION 	149
#define SLAPI_CONN_IS_SSL_SESSION 	747
#define SLAPI_CONN_CERT				743
#define SLAPI_CONN_AUTHMETHOD			746

/* 
 * Types of authentication for SLAPI_CONN_AUTHMETHOD
 * (and deprecated SLAPI_CONN_AUTHTYPE)
 */
#define SLAPD_AUTH_NONE   "none"
#define SLAPD_AUTH_SIMPLE "simple"
#define SLAPD_AUTH_SSL    "SSL"
#define SLAPD_AUTH_SASL   "SASL " /* followed by the mechanism name */


/* Command Line Arguments */
#define SLAPI_ARGC				147
#define SLAPI_ARGV				148

/* Slapd config file directory */
#define SLAPI_CONFIG_DIRECTORY			281

/* DSE flags */
#define SLAPI_DSE_DONT_WRITE_WHEN_ADDING	282
#define SLAPI_DSE_MERGE_WHEN_ADDING		283
#define SLAPI_DSE_DONT_CHECK_DUPS		284
#define SLAPI_DSE_REAPPLY_MODS			287
#define SLAPI_DSE_IS_PRIMARY_FILE		289

/* internal schema flags */
#define SLAPI_SCHEMA_USER_DEFINED_ONLY		285

/* urp flags */
#define SLAPI_URP_NAMING_COLLISION_DN	286
#define SLAPI_URP_TOMBSTONE_UNIQUEID	288

/* common to all plugins */
#define SLAPI_PLUGIN				3
#define SLAPI_PLUGIN_PRIVATE			4
#define SLAPI_PLUGIN_TYPE			5
#define SLAPI_PLUGIN_ARGV			6
#define SLAPI_PLUGIN_ARGC			7
#define SLAPI_PLUGIN_VERSION			8

#define SLAPI_PLUGIN_OPRETURN			9
#define SLAPI_PLUGIN_OBJECT			10
#define SLAPI_PLUGIN_DESTROY_FN			11

#define SLAPI_PLUGIN_DESCRIPTION		12
typedef struct slapi_plugindesc {
	char	*spd_id;
	char	*spd_vendor;
	char	*spd_version;
	char	*spd_description;	
} Slapi_PluginDesc;

#define SLAPI_PLUGIN_IDENTITY                   13

/* common for internal plugin_ops */
#define SLAPI_PLUGIN_INTOP_RESULT		15
#define SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES	16
#define SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS	17

/* miscellaneous plugin functions */
#define SLAPI_PLUGIN_CLOSE_FN			210
#define SLAPI_PLUGIN_START_FN			212
#define	SLAPI_PLUGIN_CLEANUP_FN                 232
#define	SLAPI_PLUGIN_POSTSTART_FN		233


/* extendedop plugin functions */
#define SLAPI_PLUGIN_EXT_OP_FN			300
#define SLAPI_PLUGIN_EXT_OP_OIDLIST		301
#define SLAPI_PLUGIN_EXT_OP_NAMELIST	302

/* preoperation plugin functions */
#define SLAPI_PLUGIN_PRE_BIND_FN		401
#define SLAPI_PLUGIN_PRE_UNBIND_FN		402
#define SLAPI_PLUGIN_PRE_SEARCH_FN		403
#define SLAPI_PLUGIN_PRE_COMPARE_FN		404
#define SLAPI_PLUGIN_PRE_MODIFY_FN		405
#define SLAPI_PLUGIN_PRE_MODRDN_FN		406
#define SLAPI_PLUGIN_PRE_ADD_FN			407
#define SLAPI_PLUGIN_PRE_DELETE_FN		408
#define SLAPI_PLUGIN_PRE_ABANDON_FN		409
#define SLAPI_PLUGIN_PRE_ENTRY_FN		410
#define SLAPI_PLUGIN_PRE_REFERRAL_FN		411
#define SLAPI_PLUGIN_PRE_RESULT_FN		412

/* internal preoperation plugin functions */
#define SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN    	420
#define SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN	421
#define SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN	422
#define SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN	423

/* preoperation plugin to the backend */
#define SLAPI_PLUGIN_BE_PRE_ADD_FN		450
#define SLAPI_PLUGIN_BE_PRE_MODIFY_FN		451
#define SLAPI_PLUGIN_BE_PRE_MODRDN_FN		452
#define SLAPI_PLUGIN_BE_PRE_DELETE_FN		453

/* postoperation plugin functions */
#define SLAPI_PLUGIN_POST_BIND_FN		501
#define SLAPI_PLUGIN_POST_UNBIND_FN		502
#define SLAPI_PLUGIN_POST_SEARCH_FN		503
#define SLAPI_PLUGIN_POST_COMPARE_FN		504
#define SLAPI_PLUGIN_POST_MODIFY_FN		505
#define SLAPI_PLUGIN_POST_MODRDN_FN		506
#define SLAPI_PLUGIN_POST_ADD_FN		507
#define SLAPI_PLUGIN_POST_DELETE_FN		508
#define SLAPI_PLUGIN_POST_ABANDON_FN		509
#define SLAPI_PLUGIN_POST_ENTRY_FN		510
#define SLAPI_PLUGIN_POST_REFERRAL_FN		511
#define SLAPI_PLUGIN_POST_RESULT_FN		512
#define SLAPI_PLUGIN_POST_SEARCH_FAIL_FN		513

/* internal preoperation plugin functions */
#define SLAPI_PLUGIN_INTERNAL_POST_ADD_FN   	520
#define SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN    521
#define SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN	522
#define SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN	523

/* postoperation plugin to the backend */
#define SLAPI_PLUGIN_BE_POST_ADD_FN		550
#define SLAPI_PLUGIN_BE_POST_MODIFY_FN		551
#define SLAPI_PLUGIN_BE_POST_MODRDN_FN		552
#define SLAPI_PLUGIN_BE_POST_DELETE_FN		553

/* matching rule plugin functions */
#define SLAPI_PLUGIN_MR_FILTER_CREATE_FN	600
#define SLAPI_PLUGIN_MR_INDEXER_CREATE_FN	601
#define SLAPI_PLUGIN_MR_FILTER_MATCH_FN		602
#define SLAPI_PLUGIN_MR_FILTER_INDEX_FN		603
#define SLAPI_PLUGIN_MR_FILTER_RESET_FN		604
#define SLAPI_PLUGIN_MR_INDEX_FN		605

/* matching rule plugin arguments */
#define SLAPI_PLUGIN_MR_OID			610
#define SLAPI_PLUGIN_MR_TYPE			611
#define SLAPI_PLUGIN_MR_VALUE			612
#define SLAPI_PLUGIN_MR_VALUES			613
#define SLAPI_PLUGIN_MR_KEYS			614
#define SLAPI_PLUGIN_MR_FILTER_REUSABLE		615
#define SLAPI_PLUGIN_MR_QUERY_OPERATOR		616
#define SLAPI_PLUGIN_MR_USAGE			617


/* Defined values of SLAPI_PLUGIN_MR_QUERY_OPERATOR: */
#define SLAPI_OP_LESS					1
#define SLAPI_OP_LESS_OR_EQUAL				2
#define SLAPI_OP_EQUAL					3
#define SLAPI_OP_GREATER_OR_EQUAL			4
#define SLAPI_OP_GREATER				5
#define SLAPI_OP_SUBSTRING				6

/* Defined values of SLAPI_PLUGIN_MR_USAGE: */
#define SLAPI_PLUGIN_MR_USAGE_INDEX		0
#define SLAPI_PLUGIN_MR_USAGE_SORT		1

/* Defined values for matchingRuleEntry accessor functions */
#define SLAPI_MATCHINGRULE_NAME                 1
#define SLAPI_MATCHINGRULE_OID                  2
#define SLAPI_MATCHINGRULE_DESC                 3
#define SLAPI_MATCHINGRULE_SYNTAX               4
#define SLAPI_MATCHINGRULE_OBSOLETE             5

/* syntax plugin functions and arguments */
#define SLAPI_PLUGIN_SYNTAX_FILTER_AVA		700
#define SLAPI_PLUGIN_SYNTAX_FILTER_SUB		701
#define SLAPI_PLUGIN_SYNTAX_VALUES2KEYS		702
#define SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA	703
#define SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB	704
#define SLAPI_PLUGIN_SYNTAX_NAMES		705
#define SLAPI_PLUGIN_SYNTAX_OID			706
#define SLAPI_PLUGIN_SYNTAX_FLAGS		707
#define SLAPI_PLUGIN_SYNTAX_COMPARE		708

/* ACL plugin functions and arguments */
#define SLAPI_PLUGIN_ACL_INIT			730
#define SLAPI_PLUGIN_ACL_SYNTAX_CHECK		731
#define SLAPI_PLUGIN_ACL_ALLOW_ACCESS		732
#define SLAPI_PLUGIN_ACL_MODS_ALLOWED		733
#define SLAPI_PLUGIN_ACL_MODS_UPDATE		734


#define ACLPLUGIN_ACCESS_DEFAULT		0
#define ACLPLUGIN_ACCESS_READ_ON_ENTRY		1
#define ACLPLUGIN_ACCESS_READ_ON_ATTR		2
#define ACLPLUGIN_ACCESS_READ_ON_VLV		3
#define ACLPLUGIN_ACCESS_MODRDN				4
#define ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS	5

/* Authorization types */
#define SLAPI_BE_MAXNESTLEVEL			742
#define SLAPI_CLIENT_DNS			745

/* Password storage scheme functions and arguments */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN		800
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN		801 /* only meaningfull for reversible encryption */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN		802

#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME		810	/* name of the method: SHA, SSHA... */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD	811	/* value sent over LDAP */
#define SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD		812	/* value from the DB */

/* entry fetch and entry store values */
#define SLAPI_PLUGIN_ENTRY_FETCH_FUNC				813 
#define SLAPI_PLUGIN_ENTRY_STORE_FUNC				814

/*
 * Defined values of SLAPI_PLUGIN_SYNTAX_FLAGS:
 */
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORKEYS			1
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING		2

/* controls we know about */
#define SLAPI_MANAGEDSAIT       		1000
#define SLAPI_PWPOLICY          		1001

/* arguments that are common to all operation */
#define SLAPI_TARGET_ADDRESS			48	/* target address (dn + uniqueid) should be normalized */
#define SLAPI_TARGET_UNIQUEID			49	/* target uniqueid of the operation */
#define SLAPI_TARGET_DN				50	/* target dn of the operation should be normalized */
#define SLAPI_REQCONTROLS			51	/* request controls */

/* Copies of entry before and after add, mod, mod[r]dn operations */
#define	SLAPI_ENTRY_PRE_OP			52
#define	SLAPI_ENTRY_POST_OP			53

/* LDAPv3 controls to be sent with the operation result */
#define SLAPI_RESCONTROLS			55
#define SLAPI_ADD_RESCONTROL			56	/* add result control */

/* Extra notes to be logged within access log RESULT lines */
#define SLAPI_OPERATION_NOTES			57
#define SLAPI_OP_NOTE_UNINDEXED		0x01

/* Allows controls to be passed before operation object is created */
#define SLAPI_CONTROLS_ARG			58

/* specify whether pblock content should be destroyed when the pblock is destroyed */
#define SLAPI_DESTROY_CONTENT       		59

/* add arguments */
#define SLAPI_ADD_TARGET			SLAPI_TARGET_DN
#define SLAPI_ADD_ENTRY				60
#define SLAPI_ADD_EXISTING_DN_ENTRY		61
#define SLAPI_ADD_PARENT_ENTRY      		62
#define SLAPI_ADD_PARENT_UNIQUEID		63
#define SLAPI_ADD_EXISTING_UNIQUEID_ENTRY	64

/* bind arguments */
#define SLAPI_BIND_TARGET			SLAPI_TARGET_DN
#define SLAPI_BIND_METHOD			70
#define SLAPI_BIND_CREDENTIALS			71	/* v3 only */
#define SLAPI_BIND_SASLMECHANISM		72	/* v3 only */
/* bind return values */
#define SLAPI_BIND_RET_SASLCREDS		73	/* v3 only */

/* compare arguments */
#define SLAPI_COMPARE_TARGET			SLAPI_TARGET_DN
#define SLAPI_COMPARE_TYPE			80
#define SLAPI_COMPARE_VALUE			81

/* delete arguments */
#define SLAPI_DELETE_TARGET			SLAPI_TARGET_DN
#define SLAPI_DELETE_EXISTING_ENTRY		SLAPI_ADD_EXISTING_DN_ENTRY
#define SLAPI_DELETE_GLUE_PARENT_ENTRY	SLAPI_ADD_PARENT_ENTRY

/* modify arguments */
#define SLAPI_MODIFY_TARGET			SLAPI_TARGET_DN
#define SLAPI_MODIFY_MODS			90
#define SLAPI_MODIFY_EXISTING_ENTRY		SLAPI_ADD_EXISTING_DN_ENTRY

/* modrdn arguments */
#define SLAPI_MODRDN_TARGET			SLAPI_TARGET_DN
#define SLAPI_MODRDN_NEWRDN			100
#define SLAPI_MODRDN_DELOLDRDN			101
#define SLAPI_MODRDN_NEWSUPERIOR        	102	/* v3 only */
#define SLAPI_MODRDN_EXISTING_ENTRY     	SLAPI_ADD_EXISTING_DN_ENTRY
#define SLAPI_MODRDN_PARENT_ENTRY       	104
#define SLAPI_MODRDN_NEWPARENT_ENTRY    	105
#define SLAPI_MODRDN_TARGET_ENTRY       	106
#define SLAPI_MODRDN_NEWSUPERIOR_ADDRESS	107

/* 
 * unnormalized dn argument (useful for MOD, MODRDN and DEL operations to carry 
 * the original non-escaped dn as introduced by the client application)
 */
#define SLAPI_ORIGINAL_TARGET_DN		109
#define SLAPI_ORIGINAL_TARGET			SLAPI_ORIGINAL_TARGET_DN

/* search arguments */
#define SLAPI_SEARCH_TARGET         SLAPI_TARGET_DN
#define SLAPI_SEARCH_SCOPE          110
#define SLAPI_SEARCH_DEREF          111
#define SLAPI_SEARCH_SIZELIMIT      112
#define SLAPI_SEARCH_TIMELIMIT      113
#define SLAPI_SEARCH_FILTER         114
#define SLAPI_SEARCH_STRFILTER      115
#define SLAPI_SEARCH_ATTRS          116
#define SLAPI_SEARCH_ATTRSONLY      117
#define SLAPI_SEARCH_IS_AND         118

/* abandon arguments */
#define SLAPI_ABANDON_MSGID			120

/* seq access arguments */
#define SLAPI_SEQ_TYPE				150
#define SLAPI_SEQ_ATTRNAME			151
#define SLAPI_SEQ_VAL				152

/* extended operation arguments */
#define SLAPI_EXT_OP_REQ_OID			160	/* v3 only */
#define SLAPI_EXT_OP_REQ_VALUE			161	/* v3 only */
/* extended operation return values */
#define SLAPI_EXT_OP_RET_OID			162	/* v3 only */
#define SLAPI_EXT_OP_RET_VALUE			163	/* v3 only */

/* extended filter arguments */
#define SLAPI_MR_FILTER_ENTRY			170	/* v3 only */
#define SLAPI_MR_FILTER_TYPE			171	/* v3 only */
#define SLAPI_MR_FILTER_VALUE			172	/* v3 only */
#define SLAPI_MR_FILTER_OID			173	/* v3 only */
#define SLAPI_MR_FILTER_DNATTRS			174	/* v3 only */

/* ldif2db arguments */
/* ldif file to convert to db */
#define SLAPI_LDIF2DB_FILE			180
/* check for duplicate values or not */
#define SLAPI_LDIF2DB_REMOVEDUPVALS		185
/* index only this attribute from existing database */
#define SLAPI_DB2INDEX_ATTRS			186
/* do not generate attribute indexes */
#define SLAPI_LDIF2DB_NOATTRINDEXES		187
/* list if DNs to include */
#define SLAPI_LDIF2DB_INCLUDE			188
/* list of DNs to exclude */
#define SLAPI_LDIF2DB_EXCLUDE			189
/* generate uniqueid */
#define SLAPI_LDIF2DB_GENERATE_UNIQUEID		175
#define SLAPI_LDIF2DB_NAMESPACEID       	177
#define SLAPI_LDIF2DB_ENCRYPT		303
#define SLAPI_DB2LDIF_DECRYPT		304
/* uniqueid generation options */
#define SLAPI_UNIQUEID_GENERATE_NONE		0	/* do not generate */
#define SLAPI_UNIQUEID_GENERATE_TIME_BASED	1	/* generate time based id */
#define SLAPI_UNIQUEID_GENERATE_NAME_BASED	2	/* generate name based id */

/* db2ldif arguments */
/* print keys or not in ldif */
#define SLAPI_DB2LDIF_PRINTKEY			183
/* filename to export */
#define SLAPI_DB2LDIF_FILE			184
/* dump uniqueid */
#define SLAPI_DB2LDIF_DUMP_UNIQUEID		176
#define SLAPI_DB2LDIF_SERVER_RUNNING	197

/* db2ldif/ldif2db/bak2db/db2bak arguments */
#define SLAPI_BACKEND_INSTANCE_NAME             178
#define SLAPI_BACKEND_TASK                      179
#define SLAPI_TASK_FLAGS                      	181

/* bulk import (online wire import) */
#define SLAPI_BULK_IMPORT_ENTRY                 182
#define SLAPI_BULK_IMPORT_STATE                 192
/* the actual states (these are not pblock args) */
#define SLAPI_BI_STATE_START    1
#define SLAPI_BI_STATE_DONE     2
#define SLAPI_BI_STATE_ADD      3
/* possible error codes from a bulk import */
#define SLAPI_BI_ERR_BUSY	-23	/* backend is busy; try later */

/* transaction arguments */
#define SLAPI_PARENT_TXN			190
#define SLAPI_TXN				191

/*
 * The following are used to pass information back and forth
 * between the front end and the back end.  The backend
 * creates a search result set as an opaque structure and
 * passes a reference to this back to the front end.  The
 * front end uses the backend's iterator entry point to
 * step through the results.  The entry, nentries, and
 * referrals options, below, are set/read by both the
 * front end and back end while stepping through the
 * search results.
 */
/* Search result set */
#define SLAPI_SEARCH_RESULT_SET			193
/* Search result - next entry returned from search result set */
#define	SLAPI_SEARCH_RESULT_ENTRY		194
#define SLAPI_SEARCH_RESULT_ENTRY_EXT           1944
/* Number of entries returned from search */
#define	SLAPI_NENTRIES				195
/* Any referrals encountered during the search */
#define SLAPI_SEARCH_REFERRALS			196

#define SLAPI_RESULT_CODE			881
#define SLAPI_RESULT_TEXT			882
#define SLAPI_RESULT_MATCHED			883

#define SLAPI_PB_RESULT_TEXT			885

/* Size of the database, in kilobytes */
#define SLAPI_DBSIZE				199


#ifdef __cplusplus
}
#endif

#endif /* _SLAPIPLUGIN */

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

#ifndef _PROTO_SLAP
#define _PROTO_SLAP

/*
 * Forward structure declarations
 */
struct dse;
struct dsecallback;

/*
 * abandon.c
 */
void do_abandon( Slapi_PBlock *pb );


/*
 * add.c
 */
void do_add( Slapi_PBlock *pb );


/*
 * attr.c
 */
void attr_done(Slapi_Attr *a);
int attr_add_valuearray(Slapi_Attr *a, Slapi_Value **vals, const char *dn);
int attr_replace(Slapi_Attr *a, Slapi_Value **vals);
int attr_check_onoff ( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );
int attr_check_minmax ( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );
const char *attr_get_syntax_oid(const Slapi_Attr *attr);

/*
 * attrlist.c
 */

void attrlist_free(Slapi_Attr *alist);
int attrlist_find_or_create(Slapi_Attr **alist, const char *type, Slapi_Attr ***a);
int attrlist_find_or_create_locking_optional(Slapi_Attr **alist, const char *type, Slapi_Attr ***a, PRBool use_lock);
void attrlist_merge( Slapi_Attr **alist, const char *type, struct berval **vals );
void attrlist_merge_valuearray( Slapi_Attr **alist, const char *type, Slapi_Value **vals );
int attrlist_delete( Slapi_Attr **attrs, const char *type );
Slapi_Attr *attrlist_find( Slapi_Attr *a, const char *type );
Slapi_Attr *attrlist_remove(Slapi_Attr **attrs, const char *type);
void attrlist_add(Slapi_Attr **attrs, Slapi_Attr *a);
int attrlist_count_subtypes(Slapi_Attr *a, const char *type);
Slapi_Attr *attrlist_find_ex( Slapi_Attr *a, const char *type, int *type_name_disposition, char** actual_type_name, void **hint );
int attrlist_replace(Slapi_Attr **alist, const char *type, struct berval **vals);
int attrlist_replace_with_flags(Slapi_Attr **alist, const char *type, struct berval **vals, int flags);

/*
 * attrsyntax.c
 */
void attr_syntax_read_lock(void);
void attr_syntax_unlock_read(void);
int attr_syntax_exists (const char *attr_name);
void attr_syntax_delete ( struct asyntaxinfo *asip );
#define SLAPI_SYNTAXLENGTH_NONE		(-1)	/* for syntaxlength parameter */
int attr_syntax_create( const char *attr_oid, char *const*attr_names,
		int num_names, const char *attr_desc, const char *attr_superior,
		const char *mr_equality, const char *mr_ordering,
		const char *mr_substring, char *const *attr_origins,
		const char *attr_syntax, int syntaxlength, unsigned long flags,
		struct asyntaxinfo **asip );
void attr_syntax_free( struct asyntaxinfo *a );
int attr_syntax_add( struct asyntaxinfo *asip );
char *attr_syntax_normalize_no_lookup( const char *s );
void attr_syntax_enumerate_attrs(AttrEnumFunc aef, void *arg, PRBool writelock);
void attr_syntax_all_clear_flag( unsigned long flag );
void attr_syntax_delete_all_not_flagged( unsigned long flag );
struct asyntaxinfo *attr_syntax_get_by_oid ( const char *oid );
struct asyntaxinfo *attr_syntax_get_by_name ( const char *name );
struct asyntaxinfo *attr_syntax_get_by_name_locking_optional ( const char *name, PRBool use_lock );
/*
 * Call attr_syntax_return() when you are done using a value returned
 * by attr_syntax_get_by_oid() or attr_syntax_get_by_name().
 */
void attr_syntax_return( struct asyntaxinfo *asi );
void attr_syntax_return_locking_optional( struct asyntaxinfo *asi, PRBool use_lock );
void attr_syntax_delete_all(void);

/*
 * value.c
 */
size_t value_size(const Slapi_Value *v);

/*
 * valueset.c
 */
int valuearray_init_bervalarray(struct berval **bvals, Slapi_Value ***cvals);
int valuearray_init_bervalarray_with_flags(struct berval **bvals, Slapi_Value ***cvals, unsigned long flags);
int valuearray_get_bervalarray(Slapi_Value **cvals, struct berval ***bvals); /* JCM SLOW FUNCTION */
void valuearray_free(Slapi_Value ***va);
Slapi_Value *valuearray_remove_value(const Slapi_Attr *a, Slapi_Value **va, const Slapi_Value *v);
void valuearray_remove_value_atindex(Slapi_Value **va, int index);
int valuearray_isempty( Slapi_Value **va);
void valuearray_update_csn(Slapi_Value **va, CSNType t, const CSN *csn);
int valuearray_count( Slapi_Value **va);
int valuearray_next_value( Slapi_Value **va, int index, Slapi_Value **v);
int valuearray_first_value( Slapi_Value **va, Slapi_Value **v );

void valuearrayfast_init(struct valuearrayfast *vaf,Slapi_Value **va);
void valuearrayfast_done(struct valuearrayfast *vaf);
void valuearrayfast_add_value(struct valuearrayfast *vaf,const Slapi_Value *v);
void valuearrayfast_add_value_passin(struct valuearrayfast *vaf,Slapi_Value *v);
void valuearrayfast_add_valuearrayfast(struct valuearrayfast *vaf,const struct valuearrayfast *vaf_add);

int valuetree_add_value( const char *type, struct slapdplugin *pi, const Slapi_Value *va, Avlnode **valuetreep);
int valuetree_add_valuearray( const char *type, struct slapdplugin *pi, Slapi_Value **va, Avlnode **valuetreep, int *duplicate_index);
void valuetree_free( Avlnode **valuetreep );

/* Valueset functions */

int valueset_isempty( const Slapi_ValueSet *vs);
Slapi_Value *valueset_find(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v);
Slapi_Value *valueset_remove_value(const Slapi_Attr *a, Slapi_ValueSet *vs, const Slapi_Value *v);
int valueset_remove_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **valuestodelete, int flags, Slapi_Value ***va_out);
int valueset_purge(Slapi_ValueSet *vs, const CSN *csn);
Slapi_Value **valueset_get_valuearray(const Slapi_ValueSet *vs);
size_t valueset_size(const Slapi_ValueSet *vs);
void valueset_add_valuearray(Slapi_ValueSet *vs, Slapi_Value **addvals);
void valueset_add_valuearray_ext(Slapi_ValueSet *vs, Slapi_Value **addvals, PRUint32 flags);
void valueset_add_string(Slapi_ValueSet *vs, const char *s, CSNType t, const CSN *csn);
void valueset_update_csn(Slapi_ValueSet *vs, CSNType t, const CSN *csn);
void valueset_add_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2);
int valueset_intersectswith_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **values, int *duplicate_index);
Slapi_ValueSet *valueset_dup(const Slapi_ValueSet *dupee);
void valueset_remove_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s);
int valueset_replace(Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **vals);
void valueset_update_csn_for_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **valuestoupdate, CSNType t, const CSN *csn, Slapi_Value ***valuesupdated);
void valueset_set_valuearray_byval(Slapi_ValueSet *vs, Slapi_Value **addvals);
void valueset_set_valuearray_passin(Slapi_ValueSet *vs, Slapi_Value **addvals);


/*
 * ava.c
 */
int get_ava( BerElement *ber, struct ava *ava );
void ava_done( struct ava *ava );
int rdn2ava( char *rdn, struct ava *ava );

/*
 * backend.c
 */
void be_init(Slapi_Backend *be, const char *type, const char *name, int isprivate, int logchanges, int sizelimit, int timelimit );
void be_done(Slapi_Backend *be);
void be_addsuffix(Slapi_Backend *be,const Slapi_DN *suffix);
Slapi_DN *be_getconfigdn(Slapi_Backend *be,Slapi_DN *dn);
Slapi_DN *be_getmonitordn(Slapi_Backend *be,Slapi_DN *dn);
int be_writeconfig (Slapi_Backend *be);

/*
 * backend_manager.c
 */
Slapi_Backend *be_new_internal(struct dse *pdse, const char *type, const char *name);
void be_replace_dse_internal(Slapi_Backend *be, struct dse *pdse);
int fedse_create_startOK(char *filename,  char *startokfilename, const char *configdir);
void be_cleanupall();
void be_flushall();
int be_remove( Slapi_Backend *be );
void g_set_defsize(int val);
void g_set_deftime(int val);
Slapi_Backend *g_get_user_backend( int n );
int g_get_defsize();
int g_get_deftime();
void be_unbindall( Connection *conn, Operation *op); 
int be_nbackends_public();
void g_incr_active_threadcnt();
void g_decr_active_threadcnt();
int g_get_active_threadcnt();

/*
 * bind.c
 */
void do_bind( Slapi_PBlock *pb );
void init_saslmechanisms( void );


/*
 * compare.c
 */
void do_compare( Slapi_PBlock *pb );

/*
 * computed.c
 */
int compute_attribute(char *type, Slapi_PBlock *pb,BerElement *ber,Slapi_Entry *e,int attrsonly,char *requested_type);
int compute_init();
int compute_terminate();


/*
 * config.c
 */
int slapd_bootstrap_config(const char *configdir);
int config_set_storagescheme();
int get_netsite_root_path(char *pathname);
int slapd_write_config ();
int config_set_port( const char *attrname, char *port, char *errorbuf, int apply );
int config_set_secureport( const char *attrname, char *port, char *errorbuf, int apply );
int config_set_SSLclientAuth( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ssl_check_hostname( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_SSL3ciphers( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_localhost( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_listenhost( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_securelistenhost( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_filename( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_switch( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_bind_switch( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_root_dn( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_map_entries( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_uidnumber_type( const char *attrname, char *value, char *errorbuf, int apply );    
int config_set_ldapi_gidnumber_type( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldapi_search_base_dn( const char *attrname, char *value, char *errorbuf, int apply );
#if defined(ENABLE_AUTO_DN_SUFFIX)
int config_set_ldapi_auto_dn_suffix( const char *attrname, char *value, char *errorbuf, int apply );   
#endif
int config_set_anon_limits_dn( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_slapi_counters( const char *attrname, char *value, char *errorbuf, int apply );   
int config_set_srvtab( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_sizelimit( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_lastmod( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_nagle( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_accesscontrol( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_security( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_readonly( const char *attrname, char *value, 	char *errorbuf, int apply );
int config_set_schemacheck( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_syntaxcheck( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_syntaxlogging( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_dn_validate_strict( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ds4_compatible_schema( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_schema_ignore_trailing_spaces( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_rootdn( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_rootpw( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_rootpwstoragescheme( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_workingdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_encryptionalias( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_threadnumber( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_maxthreadsperconn( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_reservedescriptors( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ioblocktimeout( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_idletimeout( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_max_filter_nest_level( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_groupevalnestlevel( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_defaultreferral( const char *attrname, struct berval **value, char *errorbuf, int apply );
int config_set_timelimit(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_errorlog_level(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_accesslog_level(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_auditlog(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_userat(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_accesslog(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_errorlog(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_pw_change(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_must_change(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pwpolicy_local(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_syntax(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minlength(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_mindigits(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minalphas(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minuppers(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minlowers(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minspecials(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_min8bit(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_maxrepeats(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_mincategories(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_mintokenlength(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_exp(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_maxage(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_minage(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_warning(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_history(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_inhistory(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_lockout(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_storagescheme(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_maxfailure(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_unlock(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_lockduration(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_resetfailurecount(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_is_global_policy(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_pw_gracelimit(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_useroc(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_return_exact_case(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_result_tweak(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_referral_mode(const char *attrname, char *url, char *errorbuf, int apply);
int config_set_conntablesize(const char *attrname, char *url, char *errorbuf, int apply);
int config_set_maxbersize(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_maxsasliosize(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_versionstring(const char *attrname,  char *versionstring, char *errorbuf, int apply );
int config_set_enquote_sup_oc(const char *attrname,  char *value, char *errorbuf, int apply );
int config_set_basedn( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_configdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_instancedir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_schemadir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_lockdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_tmpdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_certdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_ldifdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_bakdir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_rundir( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_saslpath( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_attrname_exceptions( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_hash_filters( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_rewrite_rfc1274( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_outbound_ldap_io_timeout( const char *attrname, char *value,
		char *errorbuf, int apply );
int config_set_unauth_binds_switch(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_require_secure_binds(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_anon_access_switch(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_minssf(const char *attrname, char *value, char *errorbuf, int apply );
int config_set_accesslogbuffering(const char *attrname, char *value, char *errorbuf, int apply);
int config_set_csnlogging(const char *attrname, char *value, char *errorbuf, int apply);
int config_set_force_sasl_external(const char *attrname, char *value, char *errorbuf, int apply );

#if !defined(_WIN32) && !defined(AIX)
int config_set_maxdescriptors( const char *attrname, char *value, char *errorbuf, int apply );
#endif /* !_WIN_32 && !AIX */

#ifndef _WIN32
int config_set_localuser( const char *attrname, char *value, char *errorbuf, int apply );
#endif /* !_WIN32 */

#ifdef MEMPOOL_EXPERIMENTAL
int config_set_mempool_switch( const char *attrname, char *value, char *errorbuf, int apply );
int config_set_mempool_maxfreelist( const char *attrname, char *value, char *errorbuf, int apply );
#endif /* MEMPOOL_EXPERIMENTAL */

int config_get_SSLclientAuth();
int config_get_ssl_check_hostname();
char *config_get_SSL3ciphers();
char *config_get_localhost();
char *config_get_listenhost();
char *config_get_securelistenhost();
char *config_get_ldapi_filename();
int config_get_ldapi_switch(); 
int config_get_ldapi_bind_switch();
char *config_get_ldapi_root_dn(); 
int config_get_ldapi_map_entries(); 
char *config_get_ldapi_uidnumber_type(); 
char *config_get_ldapi_gidnumber_type(); 
char *config_get_ldapi_search_base_dn(); 
#if defined(ENABLE_AUTO_DN_SUFFIX)
char *config_get_ldapi_auto_dn_suffix(); 
#endif
char *config_get_anon_limits_dn();
int config_get_slapi_counters(); 
char *config_get_srvtab();
int config_get_sizelimit();
char *config_get_pw_storagescheme();
int config_get_pw_change();
int config_get_pw_history();
int config_get_pw_must_change();
int config_get_pw_syntax();
int config_get_pw_minlength();
int config_get_pw_mindigits();
int config_get_pw_minalphas();
int config_get_pw_minuppers();
int config_get_pw_minlowers();
int config_get_pw_minspecials();
int config_get_pw_min8bit();
int config_get_pw_maxrepeats();
int config_get_pw_mincategories();
int config_get_pw_mintokenlength();
int config_get_pw_maxfailure();
int config_get_pw_inhistory();
long config_get_pw_lockduration();
long config_get_pw_resetfailurecount();
int config_get_pw_exp();
int config_get_pw_unlock();
int config_get_pw_lockout();
int config_get_pw_gracelimit();
int config_get_lastmod();
int config_get_nagle();
int config_get_accesscontrol();
int config_get_return_exact_case();
int config_get_result_tweak();
int config_get_security();
int config_get_schemacheck();
int config_get_syntaxcheck();
int config_get_syntaxlogging();
int config_get_dn_validate_strict();
int config_get_ds4_compatible_schema();
int config_get_schema_ignore_trailing_spaces();
char *config_get_rootdn();
char *config_get_rootpw();
char *config_get_rootpwstoragescheme();
#ifndef _WIN32
char *config_get_localuser();
#endif /* _WIN32 */
char *config_get_workingdir();
char *config_get_encryptionalias();
int config_get_threadnumber();
int config_get_maxthreadsperconn();
#if !defined(_WIN32) && !defined(AIX)
int config_get_maxdescriptors();
#endif /* !_WIN32 && !AIX */
int config_get_reservedescriptors();
int config_get_ioblocktimeout();
int config_get_idletimeout();
int config_get_max_filter_nest_level();
int config_get_groupevalnestlevel();
struct berval **config_get_defaultreferral();
char *config_get_userat();
int config_get_timelimit();
char* config_get_useroc();
char *config_get_accesslog();
char *config_get_errorlog();
char *config_get_auditlog();
long config_get_pw_maxage();
long config_get_pw_minage();
long config_get_pw_warning();
int config_get_errorlog_level();
int config_get_accesslog_level();
int config_get_auditlog_logging_enabled();
char *config_get_referral_mode(void);
int config_get_conntablesize(void);
int config_check_referral_mode(void);
ber_len_t config_get_maxbersize();
size_t config_get_maxsasliosize();
char *config_get_versionstring();
char *config_get_buildnum(void);
int config_get_enquote_sup_oc();
char *config_get_basedn();
char *config_get_configdir();
char *config_get_schemadir();
char *config_get_lockdir();
char *config_get_tmpdir();
char *config_get_certdir();
char *config_get_ldifdir();
char *config_get_bakdir();
char *config_get_rundir();
char *config_get_saslpath();
char **config_get_errorlog_list();
char **config_get_accesslog_list();
char **config_get_auditlog_list();
int config_get_attrname_exceptions();
int config_get_hash_filters();
int config_get_rewrite_rfc1274();
int config_get_outbound_ldap_io_timeout(void);
int config_get_unauth_binds_switch(void);
int config_get_require_secure_binds(void);
int config_get_anon_access_switch(void);
int config_get_minssf(void);
int config_get_csnlogging();
#ifdef MEMPOOL_EXPERIMENTAL
int config_get_mempool_switch();
int config_get_mempool_maxfreelist();
long config_get_system_page_size();
int config_get_system_page_bits();
#endif
int config_get_force_sasl_external();

int is_abspath(const char *);
char* rel2abspath( char * );
char* rel2abspath_ext( char *, char * );

/*
 * configdse.c
 */
int read_config_dse (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int load_config_dse(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int modify_config_dse(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int postop_modify_config_dse(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int add_root_dse( Slapi_PBlock *pb );
int load_plugin_entry(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);


/*
 * controls.c
 */
void init_controls( void );
int get_ldapmessage_controls( Slapi_PBlock *pb, BerElement *ber,
	LDAPControl ***controls );
int get_ldapmessage_controls_ext( Slapi_PBlock *pb, BerElement *ber,
	LDAPControl ***controls, int ignore_criticality );
int write_controls( BerElement *ber, LDAPControl **ctrls );
void add_control( LDAPControl ***ctrlsp, LDAPControl *newctrl );


/*
 * delete.c
 */
void do_delete( Slapi_PBlock *pb );


/*
 * detach.c
 */
int detach( int slapd_exemode, int importexport_encrypt,
			int s_port, daemon_ports_t *ports_info );
#ifndef _WIN32
void close_all_files( void );
#endif
void raise_process_limits( void );


/*
 * dn.c
 */
char *substr_dn_normalize( char *dn, char *end );
int hexchar2int( char c );


/*
 * dynalib.c
 */
void *sym_load( char *libpath, char *symbol, char *plugin, int report_errors );
/* same as above but
 * load_now - use PR_LD_NOW so that all referenced symbols are loaded immediately
 *            default is PR_LD_LAZY which only loads symbols as they are referenced
 * load_global - use PR_LD_GLOBAL so that all loaded symbols are made available globally
 *               to all other dynamically loaded libraries - default is PR_LD_LOCAL
 *               which only makes symbols visible to the executable
 */
void *sym_load_with_flags( char *libpath, char *symbol, char *plugin, int report_errors, PRBool load_now, PRBool load_global );


/*
 * filter.c
 */
int get_filter( Connection *conn, BerElement *ber, int scope,
	struct slapi_filter **filt, char **fstr );
void filter_print( struct slapi_filter *f );
void filter_normalize( struct slapi_filter *f );


/*
 * filtercmp.c
 */
void filter_compute_hash(struct slapi_filter *f);
void set_hash_filters(int i);


/*
 * filterentry.c
 */
void filter_strcpy_special( char *d, char *s );
#define FILTER_STRCPY_ESCAPE_RECHARS 0x01
void filter_strcpy_special_ext( char *d, char *s, int flags );


/*
 * entry.c
 */
int is_rootdse( const char *dn );
int get_entry_object_type();
int entry_computed_attr_init();
void send_referrals_from_entry(Slapi_PBlock *pb, Slapi_Entry *referral);


/*
 * dse.c
 */
#define DSE_OPERATION_READ 0x100
#define DSE_OPERATION_WRITE 0x200

#define DSE_BACKEND             "frontend-internal"
#define DSE_SCHEMA              "schema-internal"

struct dse *dse_new( char *filename, char *tmpfilename, char *backfilename, char *startokfilename, const char *configdir);
struct dse *dse_new_with_filelist(char *filename, char *tmpfilename, char *backfilename, char *startokfilename, const char *configdir, char **filelist);
int dse_deletedse(Slapi_PBlock *pb);
int dse_destroy(struct dse *pdse);
int dse_read_file(struct dse *pdse, Slapi_PBlock *pb);
int dse_bind( Slapi_PBlock *pb );
int dse_unbind( Slapi_PBlock *pb );
int dse_search(Slapi_PBlock *pb);
int dse_modify(Slapi_PBlock *pb);
int dse_add(Slapi_PBlock *pb);
int dse_delete(Slapi_PBlock *pb);
struct dse_callback *dse_register_callback(struct dse* pdse, int operation, int flags, const Slapi_DN *base, int scope, const char *filter, dseCallbackFn fn, void *fn_arg);
void dse_remove_callback(struct dse* pdse, int operation, int flags, const Slapi_DN *base, int scope, const char *filter, dseCallbackFn fn);
void dse_set_dont_ever_write_dse_files(void);
void dse_unset_dont_ever_write_dse_files(void);
int dse_next_search_entry (Slapi_PBlock *pb);
char *dse_read_next_entry( char *buf, char **lastp );
void dse_search_set_release (void **ss);
void dse_prev_search_results (void *pb);


/*
 * fedse.c
 */

int setup_internal_backends();


/*
 * extendedop.c
 */
void ldapi_init_extended_ops( void );
void ldapi_register_extended_op( char **opoids );
void do_extended( Slapi_PBlock *pb );


/*
 * house.c
 */
PRThread* housekeeping_start(time_t cur_time, void *arg);
void housekeeping_stop();

/*
 * lock.c
 */
FILE * lock_fopen( char *fname, char *type, FILE **lfp );
int lock_fclose( FILE *fp, FILE *lfp );


/*
 * log.c
 */
int slapd_log_error_proc( char *subsystem, char *fmt, ... );

int slapi_log_access( int level, char *fmt, ... )
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 2, 3)));
#else
        ;
#endif
int slapd_log_audit_proc(char *buffer, int buf_len);
void log_access_flush();


int access_log_openf( char *pathname, int locked);
int error_log_openf( char *pathname, int locked);
int audit_log_openf( char *pathname, int locked);

void g_set_detached(int);
void g_log_init(int log_enabled);
char *g_get_access_log();
char *g_get_error_log();
char *g_get_audit_log();
void g_set_accesslog_level(int val);

int log_set_mode(const char *attrname, char *mode_str, int logtype, char *errorbuf, int apply);
int log_set_numlogsperdir(const char *attrname, char *numlogs_str, int logtype, char *errorbuf, int apply);
int log_set_logsize(const char *attrname, char *logsize_str, int logtype, char *errorbuf, int apply);
int log_set_rotationsync_enabled(const char *attrname, char *rsync_str, int logtype, char *errorbuf, int apply);
int log_set_rotationsynchour(const char *attrname, char *rhour_str, int logtype, char *errorbuf, int apply);
int log_set_rotationsyncmin(const char *attrname, char *rmin_str, int logtype, char *errorbuf, int apply);
int log_set_rotationtime(const char *attrname, char *rtime_str, int logtype, char *errorbuf, int apply);
int log_set_rotationtimeunit(const char *attrname, char *runit, int logtype, char *errorbuf, int apply);
int log_set_maxdiskspace(const char *attrname, char *maxdiskspace_str, int logtype, char *errorbuf, int apply);
int log_set_mindiskspace(const char *attrname, char *minfreespace_str, int logtype, char *errorbuf, int apply);
int log_set_expirationtime(const char *attrname, char *exptime_str , int logtype, char *errorbuf, int apply);
int log_set_expirationtimeunit(const char *attrname, char *expunit, int logtype, char *errorbuf, int apply);
char **log_get_loglist(int logtype);
int  log_update_accesslogdir(char *pathname, int apply);
int  log_update_errorlogdir(char *pathname, int apply);
int  log_update_auditlogdir(char *pathname, int apply);
int  log_set_logging (const char *attrname, char *value, int logtype, char *errorbuf, int apply);
int check_log_max_size(
                    char *maxdiskspace_str,
                    char *mlogsize_str,
                    int maxdiskspace,
                    int mlogsize,
					char * returntext,
                    int logtype);
 

void g_set_accesslog_level(int val);


/*
 * util.c
 */
void slapd_nasty(char* str, int c, int err);
int strarray2str( char **a, char *buf, size_t buflen, int include_quotes );
#ifndef _WIN32
int slapd_chown_if_not_owner(const char *filename, uid_t uid, gid_t gid);
#endif
int slapd_comp_path(char *p0, char *p1);


/*
 * modify.c
 */
void do_modify( Slapi_PBlock *pb );

/*
 * modrdn.c
 */
void do_modrdn( Slapi_PBlock *pb );


/*
 * modutil.c
 */
int entry_replace_values( Slapi_Entry *e, const char *type, struct berval **vals );
int entry_replace_values_with_flags( Slapi_Entry *e, const char *type, struct berval **vals, int flags );
int entry_apply_mods( Slapi_Entry *e, LDAPMod **mods );
int entry_apply_mod( Slapi_Entry *e, const LDAPMod *mod );
void freepmods( LDAPMod **pmods );


/*
 * monitor.c
 */
int monitor_info( Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
char *slapd_get_version_value( void );



/*
 * operation.c
 */
Slapi_Operation *operation_new(int flags);
void operation_free( Slapi_Operation **op, Connection *conn );
int slapi_op_abandoned( Slapi_PBlock *pb );
void operation_out_of_disk_space();
int get_operation_object_type();
Slapi_DN* operation_get_target_spec (Slapi_Operation *op);
void operation_set_target_spec (Slapi_Operation *op, const Slapi_DN *target_spec);
void operation_set_target_spec_str (Slapi_Operation *op, const char *target_spec);
unsigned long operation_get_abandoned_op (const Slapi_Operation *op);
void operation_set_abandoned_op (Slapi_Operation *op, unsigned long abndoned_op);
void operation_set_type(Slapi_Operation *op, unsigned long type);


/*
 * plugin.c
 */
int plugin_call_plugins( Slapi_PBlock *, int );
int plugin_setup(Slapi_Entry *plugin_entry, struct slapi_componentid *group,
	slapi_plugin_init_fnptr initfunc, int add_to_dit);
int plugin_call_exop_plugins( Slapi_PBlock *pb, char *oid );
const char *plugin_extended_op_oid2string( const char *oid );
void plugin_closeall(int close_backends, int close_globals);
void plugin_startall(int argc,char **argv,int start_backends, int start_global);
struct slapdplugin *get_plugin_list(int plugin_list_index);
PRBool plugin_invoke_plugin_sdn (struct slapdplugin *plugin, int operation, Slapi_PBlock *pb, Slapi_DN *target_spec);
struct slapdplugin *plugin_get_by_name(char *name);
struct slapdplugin *plugin_get_pwd_storage_scheme(char *name, int len, int index);
char *plugin_get_pwd_storage_scheme_list(int index);
int plugin_add_descriptive_attributes( Slapi_Entry *e,
		struct slapdplugin *plugin );
void plugin_call_entryfetch_plugins(char **entrystr, uint *size);
void plugin_call_entrystore_plugins(char **entrystr, uint *size);
void plugin_print_versions(void);
void plugin_print_lists(void);

/*
 * plugin_mr.c
 */
struct slapdplugin *slapi_get_global_mr_plugins();
int plugin_mr_filter_create (mr_filter_t* f);

/*
 * plugin_syntax.c
 */
struct slapdplugin *slapi_get_global_syntax_plugins();
int plugin_call_syntax_filter_ava( const Slapi_Attr *a, int ftype, struct ava *ava );
int plugin_call_syntax_filter_ava_sv( const Slapi_Attr *a, int ftype, struct ava *ava, Slapi_Value **retVal, int useDeletedValues );
int plugin_call_syntax_filter_sub( Slapi_PBlock *pb, Slapi_Attr *a, struct subfilt *fsub );
int plugin_call_syntax_filter_sub_sv( Slapi_PBlock *pb, Slapi_Attr *a, struct subfilt *fsub );
int plugin_call_syntax_get_compare_fn(void *vpi, value_compare_fn_type *compare_fn);
struct slapdplugin *plugin_syntax_find( const char *nameoroid );
void plugin_syntax_enumerate( SyntaxEnumFunc sef, void *arg );
char *plugin_syntax2oid( struct slapdplugin *pi );

/*
 * plugin_acl.c
 */
int plugin_call_acl_plugin ( Slapi_PBlock *pb, Slapi_Entry *e,
	char **attrs, struct berval *val, int access, int flags, char **errbuf);
int plugin_call_acl_mods_access ( Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, char **errbuf );
int plugin_call_acl_mods_update ( Slapi_PBlock *pb, int optype );
int plugin_call_acl_verify_syntax ( Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf );

/*
 * pw_mgmt.c
 */
void pw_init( void );
int need_new_pw( Slapi_PBlock *pb, long *t,  Slapi_Entry *e, int pwresponse_req );
int update_pw_info( Slapi_PBlock *pb , char *old_pw );
int check_pw_syntax( Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals, 
	char **old_pw, Slapi_Entry *e, int mod_op );
int check_pw_syntax_ext( Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals,
	char **old_pw, Slapi_Entry *e, int mod_op, Slapi_Mods *smods );
int check_account_lock( Slapi_PBlock *pb, Slapi_Entry * bind_target_entry, int pwresponse_req, int account_inactivation_only /*no wire/no pw policy*/);
int check_pw_minage( Slapi_PBlock *pb, const Slapi_DN *sdn, struct berval **vals) ;
void add_password_attrs( Slapi_PBlock *pb, Operation *op, Slapi_Entry *e );
void mod_allowchange_aci(char *val);
void pw_mod_allowchange_aci(int pw_prohibit_change);
void pw_add_allowchange_aci(Slapi_Entry *e, int pw_prohibit_change);

/*
 * pw_retry.c
 */
int update_pw_retry ( Slapi_PBlock *pb );
void pw_apply_mods(const char *dn, Slapi_Mods *mods);
void pw_set_componentID(struct slapi_componentid * cid);
struct slapi_componentid * pw_get_componentID();

/*
 * referral.c
 */
void referrals_free ();
struct berval **ref_adjust( Slapi_PBlock *pb, struct berval **urls, const Slapi_DN *refcontainerdn, int is_reference );
/* GGOODREPL temporarily in slapi-plugin.h struct berval **get_data_source( char *dn, int orc, Ref_Array * ); */


/*
 * resourcelimit.c
 */
int reslimit_update_from_dn( Slapi_Connection *conn, Slapi_DN *dn );
int reslimit_update_from_entry( Slapi_Connection *conn, Slapi_Entry *e );
void reslimit_cleanup( void );


/*
 * result.c
 */
void g_set_num_entries_sent( Slapi_Counter *counter );
PRUint64 g_get_num_entries_sent();
void g_set_num_bytes_sent( Slapi_Counter *counter );
PRUint64 g_get_num_bytes_sent();
void g_set_default_referral( struct berval **ldap_url );
struct berval	**g_get_default_referral();
void disconnect_server( Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error );
int send_ldap_search_entry( Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **ectrls,
	char **attrs, int attrsonly );
void send_ldap_result( Slapi_PBlock *pb, int err, char *matched, char *text,
	int nentries, struct berval **urls );
int send_ldap_search_entry_ext( Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **ectrls,
	char **attrs, int attrsonly, int send_result, int nentries, struct berval **urls );
void send_ldap_result_ext( Slapi_PBlock *pb, int err, char *matched, char *text,
	int nentries, struct berval **urls, BerElement	*ber );
void send_nobackend_ldap_result( Slapi_PBlock *pb );
int send_ldap_referral( Slapi_PBlock *pb, Slapi_Entry *e, struct berval **refs,
	struct berval ***urls );
int send_ldapv3_referral( Slapi_PBlock *pb, struct berval **urls );
int set_db_default_result_handlers(Slapi_PBlock *pb);
void disconnect_server_nomutex( Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error );
long g_get_current_conn_count();
void g_increment_current_conn_count();
void g_decrement_current_conn_count();
void g_set_current_conn_count_mutex( PRLock *plock );
PRLock *g_get_current_conn_count_mutex();
int encode_attr(Slapi_PBlock *pb,BerElement *ber,Slapi_Entry *e,Slapi_Attr *a,int attrsonly,char *type);


/*
 * schema.c
 */
int init_schema_dse(const char *configdir);
char *oc_find_name( const char *name_or_oid );
int oc_schema_check( Slapi_Entry *e );
int modify_schema_dse (Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int read_schema_dse ( Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
void oc_lock_read( void );
void oc_lock_write( void );
void oc_unlock( void );
/* Note: callers of g_get_global_oc_nolock() must hold a read or write lock */
struct objclass* g_get_global_oc_nolock();
/* Note: callers of g_set_global_oc_nolock() must hold a write lock */
void g_set_global_oc_nolock(struct objclass *newglobaloc);
/* Note: callers of g_get_global_schema_csn() must hold a read lock */
const CSN *g_get_global_schema_csn();
/* Note: callers of g_set_global_schema_csn() must hold a write lock. */
/* csn is consumed. */
void g_set_global_schema_csn(CSN *csn);
void slapi_schema_expand_objectclasses( Slapi_Entry *e );
/* API to validate the schema files */
int slapi_validate_schema_files(char *schemadir);
/* API to reload the schema files */
int slapi_reload_schema_files(char *schemadir);

/*
 * schemaparse.c
 */
void normalize_oc( void );
void normalize_oc_nolock( void );
/* Note: callers of oc_update_inheritance_nolock() must hold a write lock */
void oc_update_inheritance_nolock( struct objclass *oc );

/*
 * search.c
 */
void do_search( Slapi_PBlock *pb );


/*
 * ssl.c
 */
int slapd_nss_init(int init_ssl, int config_available);
int slapd_ssl_init();
int slapd_ssl_init2(PRFileDesc **fd, int startTLS);
int slapd_security_library_is_initialized();
int slapd_ssl_listener_is_initialized();
int slapd_SSL_client_auth (LDAP* ld);

/*
 * security_wrappers.c
 */
int slapd_ssl_handshakeCallback(PRFileDesc *fd, void * callback, void * client_data);
int slapd_ssl_badCertHook(PRFileDesc *fd, void * callback, void * client_data);
CERTCertificate * slapd_ssl_peerCertificate(PRFileDesc *fd);
SECStatus slapd_ssl_getChannelInfo(PRFileDesc *fd, SSLChannelInfo *sinfo, PRUintn len);
SECStatus slapd_ssl_getCipherSuiteInfo(PRUint16 ciphersuite, SSLCipherSuiteInfo *cinfo, PRUintn len);
PRFileDesc * slapd_ssl_importFD(PRFileDesc *model, PRFileDesc *fd);
SECStatus slapd_ssl_resetHandshake(PRFileDesc *fd, PRBool asServer);
void slapd_pk11_configurePKCS11(char *man, char *libdes, char *tokdes, char *ptokdes,
				char *slotdes, char *pslotdes, char *fslotdes, 
				char *fpslotdes, int minPwd,
				int pwdRequired);
void slapd_pk11_freeSlot(PK11SlotInfo *slot);
void slapd_pk11_freeSymKey(PK11SymKey *key);
PK11SlotInfo *slapd_pk11_findSlotByName(char *name);
SECAlgorithmID *slapd_pk11_createPBEAlgorithmID(SECOidTag algorithm, int iteration, SECItem *salt);
PK11SymKey *slapd_pk11_pbeKeyGen(PK11SlotInfo *slot, SECAlgorithmID *algid,  SECItem *pwitem,
				 PRBool faulty3DES, void *wincx);
CK_MECHANISM_TYPE slapd_pk11_algtagToMechanism(SECOidTag algTag);
SECItem *slapd_pk11_paramFromAlgid(SECAlgorithmID *algid);
CK_RV slapd_pk11_mapPBEMechanismToCryptoMechanism(CK_MECHANISM_PTR pPBEMechanism,
						  CK_MECHANISM_PTR pCryptoMechanism,
						  SECItem *pbe_pwd, PRBool bad3DES);
int slapd_pk11_getBlockSize(CK_MECHANISM_TYPE type,SECItem *params);
PK11Context * slapd_pk11_createContextBySymKey(CK_MECHANISM_TYPE type,
					       CK_ATTRIBUTE_TYPE operation, 
					       PK11SymKey *symKey, SECItem *param);
SECStatus slapd_pk11_cipherOp(PK11Context *context, unsigned char * out, int *outlen,
			      int maxout, unsigned char *in, int inlen);
SECStatus slapd_pk11_finalize(PK11Context *context);
PK11SlotInfo *slapd_pk11_getInternalKeySlot();
PK11SlotInfo *slapd_pk11_getInternalSlot();
SECStatus slapd_pk11_authenticate(PK11SlotInfo *slot, PRBool loadCerts, void *wincx);
void slapd_pk11_setSlotPWValues(PK11SlotInfo *slot,int askpw, int timeout);
PRBool slapd_pk11_isFIPS();
CERTCertificate *slapd_pk11_findCertFromNickname(char *nickname, void *wincx);
SECKEYPrivateKey *slapd_pk11_findKeyByAnyCert(CERTCertificate *cert, void *wincx);
PRBool slapd_pk11_fortezzaHasKEA(CERTCertificate *cert);
void slapd_pk11_destroyContext(PK11Context *context, PRBool freeit);
void secoid_destroyAlgorithmID(SECAlgorithmID *algid, PRBool freeit);
void slapd_pk11_CERT_DestroyCertificate(CERTCertificate *cert);
SECKEYPublicKey *slapd_CERT_ExtractPublicKey(CERTCertificate *cert);
SECKEYPrivateKey * slapd_pk11_FindPrivateKeyFromCert(PK11SlotInfo *slot,CERTCertificate *cert, void *wincx);
PK11SlotInfo *slapd_pk11_GetInternalKeySlot(void);
SECStatus slapd_pk11_PubWrapSymKey(CK_MECHANISM_TYPE type, SECKEYPublicKey *pubKey,PK11SymKey *symKey, SECItem *wrappedKey);
PK11SymKey *slapd_pk11_KeyGen(PK11SlotInfo *slot,CK_MECHANISM_TYPE type,SECItem *param, int keySize,void *wincx);
void slapd_pk11_FreeSlot(PK11SlotInfo *slot);
void slapd_pk11_FreeSymKey(PK11SymKey *key);
void slapd_pk11_DestroyContext(PK11Context *context, PRBool freeit);
SECItem *slapd_pk11_ParamFromIV(CK_MECHANISM_TYPE type,SECItem *iv);
PK11SymKey *slapd_pk11_PubUnwrapSymKey(SECKEYPrivateKey *wrappingKey, SECItem *wrappedKey,CK_MECHANISM_TYPE target, CK_ATTRIBUTE_TYPE operation, int keySize);
unsigned slapd_SECKEY_PublicKeyStrength(SECKEYPublicKey *pubk);
SECStatus slapd_pk11_Finalize(PK11Context *context);
SECStatus slapd_pk11_DigestFinal(PK11Context *context, unsigned char *data,unsigned int *outLen, unsigned int length);
void slapd_SECITEM_FreeItem (SECItem *zap, PRBool freeit);
void slapd_pk11_DestroyPrivateKey(SECKEYPrivateKey *key);
void slapd_pk11_DestroyPublicKey(SECKEYPublicKey *key);

/*
 * start_tls_extop.c
 */
int start_tls( Slapi_PBlock *pb );
int start_tls_graceful_closure( Connection *conn, Slapi_PBlock *pb, int is_initiator );
int start_tls_register_plugin();
int start_tls_init( Slapi_PBlock *pb );

/* passwd_extop.c */
int passwd_modify_register_plugin();

/*
 * slapi_str2filter.c
 */
struct slapi_filter *slapi_str2filter( char *str );
char   *slapi_find_matching_paren( const char *str);
struct slapi_filter    *str2simple();


/*
 * time.c
 */
char *get_timestring(time_t *t);
void free_timestring(char *timestr);
time_t current_time();
time_t poll_current_time();
char* format_localTime (time_t from);
time_t parse_localTime (char* from);

#ifndef HAVE_TIME_R
int gmtime_r( const time_t *timer, struct tm *result );
int localtime_r( const time_t *timer, struct tm *result );
int ctime_r( const time_t *timer, char *buffer, int buflen );
#endif

char *format_genTime (time_t from);
time_t parse_genTime (char *from);

/*
 * unbind.c
 */
void do_unbind( Slapi_PBlock *pb );


/*
 * pblock.c
 */
void pblock_init( Slapi_PBlock *pb );
void pblock_init_common( Slapi_PBlock *pb, Slapi_Backend *be, Connection *conn, Operation *op );
void pblock_done( Slapi_PBlock *pb );
void bind_credentials_set( Connection *conn,
                char *authtype, char *normdn,
                char *extauthtype, char *externaldn, CERTCertificate *clientcert , Slapi_Entry * binded);
void bind_credentials_set_nolock( Connection *conn,
		char *authtype, char *normdn,
		char *extauthtype, char *externaldn, CERTCertificate *clientcert , Slapi_Entry * binded);
void bind_credentials_clear( Connection *conn, PRBool lock_conn,
		PRBool clear_externalcreds );


/* 
 * libglobs.c
 */
void g_set_shutdown( int reason );
int g_get_shutdown();
void c_set_shutdown();
int c_get_shutdown();
int	  g_get_global_lastmod();
/* Ref_Array *g_get_global_referrals(void); */
struct snmp_vars_t * g_get_global_snmp_vars();
void FrontendConfig_init();
int g_get_slapd_security_on();
char *config_get_versionstring();

void libldap_init_debug_level(int *);
int get_entry_point( int, caddr_t* );

int config_set_entry(Slapi_Entry *e);
int config_set(const char *attr_name, struct berval **values, char *errorbuf, int apply);

void free_pw_scheme(struct pw_scheme *pwsp);

/*
 * rootdse.c
 */
int read_root_dse( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg );
int modify_root_dse( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg );

/*
 * psearch.c
 */
void ps_init_psearch_system();
void ps_stop_psearch_system();
void ps_add( Slapi_PBlock *pb, ber_int_t changetypes, int send_entchg_controls );
void ps_wakeup_all();
void ps_service_persistent_searches( Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype,
	ber_int_t chgnum );
int ps_parse_control_value( struct berval *psbvp, ber_int_t *changetypesp,
    int *changesonlyp, int *returnecsp );

/*
 * globals.c
 */
void set_entry_points();

/*
 * defbackend.c
 */
void defbackend_init( void );
Slapi_Backend *defbackend_get_backend( void );

/*
 * plugin_internal_op.c
 */
void do_disconnect_server( Connection *conn, PRUint64 opconnid, int opid );

/*
 * secpwd.c
 */
char* getPassword();

/*
 * match.c
 */
struct matchingRuleList *g_get_global_mrl();
void g_set_global_mrl(struct matchingRuleList *newglobalmrl);

/*
 * generation.c
 */

/* 
 * factory.c
 */

int factory_register_type(const char *name,size_t offset);
void *factory_create_extension(int type,void *object,void *parent);
void factory_destroy_extension(int type,void *object,void *parent,void **extension);

/*
 * auditlog.c
 */

void write_audit_log_entry( Slapi_PBlock *pb);

/*
 * eventq.c
 */
void eq_init();
void eq_start();
void eq_stop();

/* 
 * uniqueidgen.c
 */

/* Function:    uniqueIDGenInit
   Description: this function initializes the generator
   Parameters:  configDir - directory in which generators state is stored
				configDN - DIT entry with state information
				mtGen - indicates whether multiple threads will use generator
   Return:      UID_SUCCESS if function succeeds
                UID_BADDATA if invalif directory is passed
                UID_SYSTEM_ERROR if any other failure occurs 
*/
int uniqueIDGenInit (const char *configDir, const Slapi_DN *configDN, PRBool mtGen);      

/* Function:    uniqueIDGenCleanup
   Description: cleanup
   Parameters:  none
   Return:      none
*/
void uniqueIDGenCleanup (); 

/*
 * init.c
 */
void slapd_init();

/*
 * plugin.c
 */

/* this interface is exposed to be used by internal operations. It assumes no dynamic 
   plugin configuration as it provides no data locking. The interface will change once 
   we decide to support dynamic configuration
 */
const char* plugin_get_name (const struct slapdplugin *plugin);
const DataList* plugin_get_target_subtrees (const struct slapdplugin *plugin);
const DataList* plugin_get_bind_subtrees (const struct slapdplugin *plugin);
int plugin_get_schema_check (const struct slapdplugin *plugin);
int plugin_get_log_access (const struct slapdplugin *plugin);
int plugin_get_log_audit (const struct slapdplugin *plugin);

/*
 * getfilelist.c
 */
char **get_filelist(
	const char *dirname, /* directory path; if NULL, uses "." */
	const char *pattern, /* regex pattern, not shell style wildcard */
	int hiddenfiles, /* if true, return hidden files and directories too */
	int nofiles, /* if true, do not return files */
	int nodirs /* if true, do not return directories */
);
void free_filelist(char **filelist);
char **get_priority_filelist(const char *directory, const char *pattern);

/* this interface is exposed to be used by internal operations. 
 */
char* plugin_get_dn (const struct slapdplugin *plugin);
/* determine whether operation should be allowed based on plugin configuration */
PRBool plugin_allow_internal_op (Slapi_DN *target, struct slapdplugin *plugin);
/* build operation action bitmap based on plugin configuration and actions specified for the operation */
int plugin_build_operation_action_bitmap (int input_actions, const struct slapdplugin *plugin);
const struct slapdplugin* plugin_get_server_plg ();

/* opshared.c - functions shared between regular and internal operations */
void op_shared_search (Slapi_PBlock *pb, int send_result);
int op_shared_is_allowed_attr (const char *attr_name, int replicated_op);
void op_shared_log_error_access (Slapi_PBlock *pb, const char *type, const char *dn, const char *msg);
int search_register_reslimits( void );


/* plugin_internal_op.c - functions shared by several internal operations */
LDAPMod **normalize_mods2bvals(const LDAPMod **mods);
Slapi_Operation* internal_operation_new(unsigned long op_type, int flags);
void internal_getresult_callback(struct conn *unused1, struct op *op, int err, char *unused2, 
								 char *unused3, int unused4, struct berval **unused5);
/* allow/disallow operation based of the plugin configuration */
PRBool allow_operation (Slapi_PBlock *pb);
/* set operation configuration based on the plugin configuration */
void set_config_params (Slapi_PBlock *pb);
/* set parameters common for all internal operations */
void set_common_params (Slapi_PBlock *pb);
void do_ps_service(Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype, ber_int_t chgnum);
void modify_update_last_modified_attr(Slapi_PBlock *pb, Slapi_Mods *smods);

/*
 * debugdump.cpp
 */
void debug_thread_wrapper(void *thread_start_function);

/*
 * counters.c
 */
void counters_as_entry(Slapi_Entry* e);
void counters_to_errors_log(const char *text);

/*
 * ch_malloc.c
 */
void slapi_ch_start_recording();
void slapi_ch_stop_recording();

/*
 * snmpcollator.c
 */
void snmp_as_entry(Slapi_Entry* e);

/*
 * subentry.c
 */
int subentry_check_filter(Slapi_Filter *f);
void subentry_create_filter(Slapi_Filter** filter);

/*
 * vattr.c
 */
void vattr_init();
void vattr_cleanup();
void vattrcache_entry_READ_LOCK(const Slapi_Entry *e);
void vattrcache_entry_READ_UNLOCK(const Slapi_Entry *e);
void vattrcache_entry_WRITE_LOCK(const Slapi_Entry *e);
void vattrcache_entry_WRITE_UNLOCK(const Slapi_Entry *e);

/*
 * slapd_plhash.c - supplement to NSPR plhash
 */
void *PL_HashTableLookup_const(
	void *ht, /* really a PLHashTable */
	const void *key);

/*
 * mapping_tree.c
 */
int mapping_tree_init();
void mapping_tree_free ();
int mapping_tree_get_extension_type ();

/*
 * connection.c
 */
int connection_acquire_nolock (Connection *conn);
int connection_release_nolock (Connection *conn);
int connection_is_free (Connection *conn);
int connection_is_active_nolock (Connection *conn);
#if defined(USE_OPENLDAP)
ber_slen_t openldap_read_function(Sockbuf_IO_Desc *sbiod, void *buf, ber_len_t len);
#endif

/*
 * saslbind.c
 */
int ids_sasl_init(void);
char **ids_sasl_listmech(Slapi_PBlock *pb);
void ids_sasl_check_bind(Slapi_PBlock *pb);
void ids_sasl_server_new(Connection *conn);
int slapd_ldap_sasl_interactive_bind(
    LDAP *ld, /* ldap connection */
    const char *bindid, /* usually a bind DN for simple bind */
    const char *creds, /* usually a password for simple bind */
    const char *mech, /* name of mechanism */
    LDAPControl **serverctrls, /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    int *msgidp /* pass in non-NULL for async handling */
);

/*
 * sasl_io.c
 */
int sasl_io_setup(Connection *c);

/*
 * daemon.c
 */
#ifndef LINUX
void slapd_do_nothing(int);
#endif
#ifndef _WIN32
void slapd_wait4child (int);
#else
void *slapd_service_exit_wait();
#endif

/*
 * main.c
 */
#if ( defined( hpux ) || defined( irix ))
void signal2sigaction( int s, void *a );
#endif
int slapd_do_all_nss_ssl_init(int slapd_exemode, int importexport_encrypt,
                              int s_port, daemon_ports_t *ports_info);

/*
 * pagedresults.c
 */
int pagedresults_parse_control_value(struct berval *psbvp, ber_int_t *pagesize, int *curr_search_count);
void pagedresults_set_response_control(Slapi_PBlock *pb, int	iscritical, ber_int_t pagesize, int curr_search_count);
Slapi_Backend *pagedresults_get_current_be(Connection *conn);
int pagedresults_set_current_be(Connection *conn, Slapi_Backend *be);
void *pagedresults_get_search_result(Connection *conn);
int pagedresults_set_search_result(Connection *conn, void *sr);
int pagedresults_get_search_result_count(Connection *conn);
int pagedresults_set_search_result_count(Connection *conn, int cnt);
int pagedresults_get_with_sort(Connection *conn);
int pagedresults_set_with_sort(Connection *conn, int flags);
int pagedresults_get_sort_result_code(Connection *conn);
int pagedresults_set_sort_result_code(Connection *conn, int code);
int pagedresults_set_timelimit(Connection *conn, time_t timelimit);
int pagedresults_cleanup(Connection *conn, int needlock);
int pagedresults_check_or_set_processing(Connection *conn);
int pagedresults_reset_processing(Connection *conn);

/*
 * sort.c
 */
int sort_make_sort_response_control(Slapi_PBlock *pb, int code, char *error_type);

#endif /* _PROTO_SLAP */

/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 * 
 * The contents of this file are subject to the Mozilla Public License Version 
 * 1.1 (the "License"); you may not use this file except in compliance with 
 * the License. You may obtain a copy of the License at 
 * http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 * 
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 * 
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 * 
 * Contributor(s):
 * 
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 * 
 * ***** END LICENSE BLOCK ***** */

#ifndef _LDAPTOOL_H
#define _LDAPTOOL_H

/* XXX:mhein The following is a workaround for the redefinition of */
/*           const problem on OSF.  Fix to be provided by NSS */
/*           This is a pretty benign workaround for us which */
/*           should not cause problems in the future even if */
/*           we forget to take it out :-) */

#ifdef OSF1V4D
#ifndef __STDC__
#  define __STDC__
#endif /* __STDC__ */
#endif /* OSF1V4D */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef AIX
#include <strings.h>
#endif


#ifdef SCOOS
#include <sys/types.h>
#endif

#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern int getopt (int argc, char *const *argv, const char *optstring);
#include <io.h>	/* for _mktemp() */
#define LDAPTOOL_MKTEMP( p )	_mktemp( p )
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define LDAPTOOL_MKTEMP( p )	mktemp( p )
#endif

#ifdef LINUX
#include <getopt.h>	/* not always included from unistd.h */
#endif

#include <ctype.h>

#ifndef SCOOS
#include <sys/types.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>

#if defined(NET_SSL)
#include <ssl.h>
#endif

#include <portable.h>
#include <ldap.h>
#include <ldaplog.h>
#include <ldif.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * shared macros, structures, etc.
 */
#define LDAPTOOL_RESULT_IS_AN_ERROR( rc ) \
		( (rc) != LDAP_SUCCESS && (rc) != LDAP_COMPARE_TRUE \
		&& (rc) != LDAP_COMPARE_FALSE )

#define LDAPTOOL_DEFSEP		"="	/* used by ldapcmp and ldapsearch */
#define LDAPTOOL_DEFHOST	"localhost"
#define LDAPTOOL_DEFCERTDBPATH	"."
#define LDAPTOOL_DEFKEYDBPATH	"."
#define LDAPTOOL_DEFREFHOPLIMIT		5

#define LDAPTOOL_SAFEREALLOC( ptr, size )  ( ptr == NULL ? malloc( size ) : \
						realloc( ptr, size ))
/* this defines the max number of control requests for the tools */
#define CONTROL_REQUESTS 50

/*
 * globals (defined in common.c)
 */
extern char		*ldaptool_host;
extern char		*ldaptool_host2;
extern int		ldaptool_port;
extern int		ldaptool_port2;
extern int		ldaptool_verbose;
extern int		ldaptool_not;
extern int		ldaptool_nobind;
extern int		ldaptool_noconv_passwd;
extern char		*ldaptool_progname;
extern FILE		*ldaptool_fp;
extern char		*ldaptool_charset;
extern LDAPControl	*ldaptool_request_ctrls[];
#ifdef LDAP_DEBUG
extern int ldaptool_dbg_lvl;
#define LDAPToolDebug(lvl,fmt,arg1,arg2,arg3) if (lvl & ldaptool_dbg_lvl) { fprintf(stderr,fmt,arg1,arg2,arg3); }
#else
#define LDAPToolDebug(lvl,fmt,arg1,arg2,arg3)
#endif /* LDAP_DEBUG */


/*
 * function prototypes
 */
void ldaptool_common_usage( int two_hosts );
int ldaptool_process_args( int argc, char **argv, char *extra_opts,
	int two_hosts, void (*extra_opt_callback)( int option, char *optarg ));
LDAP *ldaptool_ldap_init( int second_host );
void ldaptool_bind( LDAP *ld );
void ldaptool_cleanup( LDAP *ld );
int ldaptool_print_lderror( LDAP *ld, char *msg, int check4ssl );
#define LDAPTOOL_CHECK4SSL_NEVER	0
#define LDAPTOOL_CHECK4SSL_ALWAYS	1
#define LDAPTOOL_CHECK4SSL_IF_APPROP	2	/* if appropriate */
LDAPControl *ldaptool_create_manage_dsait_control( void );
void ldaptool_print_referrals( char **refs );
int ldaptool_print_extended_response( LDAP *ld, LDAPMessage *res, char *msg );
LDAPControl *ldaptool_create_proxyauth_control( LDAP *ld );
LDAPControl *ldaptool_create_geteffectiveRights_control ( LDAP *ld,
        const char *authzid, const char **attrlist );
void ldaptool_add_control_to_array( LDAPControl *ctrl, LDAPControl **array);
void ldaptool_reset_control_array( LDAPControl **array );
char *ldaptool_get_tmp_dir( void );
char *ldaptool_local2UTF8( const char *s, const char *desc );
char *ldaptool_getpass( const char *prompt );
char *ldaptool_read_password( FILE *mod_password_fp );
int ldaptool_berval_is_ascii( const struct berval *bvp );
int ldaptool_sasl_bind_s( LDAP *ld, const char *dn, const char *mechanism,
        const struct berval *cred, LDAPControl **serverctrls,
        LDAPControl **clientctrls, struct berval **servercredp, char *msg );
int ldaptool_simple_bind_s( LDAP *ld, const char *dn, const char *passwd,
	LDAPControl **serverctrls, LDAPControl **clientctrls, char *msg );
int ldaptool_add_ext_s( LDAP *ld, const char *dn, LDAPMod **attrs,
        LDAPControl **serverctrls, LDAPControl **clientctrls, char *msg );
int ldaptool_modify_ext_s( LDAP *ld, const char *dn, LDAPMod **mods,
        LDAPControl **serverctrls, LDAPControl **clientctrls, char *msg );
int ldaptool_delete_ext_s( LDAP *ld, const char *dn, LDAPControl **serverctrls,
        LDAPControl **clientctrls, char *msg );
int ldaptool_rename_s(  LDAP *ld, const char *dn, const char *newrdn,
        const char *newparent, int deleteoldrdn, LDAPControl **serverctrls,
        LDAPControl **clientctrls, char *msg );
int ldaptool_compare_ext_s( LDAP *ld, const char *dn, const char *attrtype,
	    const struct berval *bvalue, LDAPControl **serverctrls,
	    LDAPControl **clientctrls, char *msg );
int ldaptool_boolean_str2value ( const char *s, int strict );
int ldaptool_parse_ctrl_arg ( char *ctrl_arg, char sep, char **ctrl_oid, 
	    int *ctrl_criticality, char **ctrl_value, int *vlen);
FILE *ldaptool_open_file ( const char *filename, const char * mode);


#ifdef __cplusplus
}
#endif

#endif /* LDAPTOOL_H */

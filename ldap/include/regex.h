/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( macintosh ) || defined( DOS ) || defined( _WINDOWS ) || defined( NEED_BSDREGEX )
/*
 * Copyright (c) 1993 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */
/*
 * regex.h -- includes for regular expression matching routines
 * 13 August 1993 Mark C Smith
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ldap.h" 

#if !defined( NEEDPROTOS ) && defined( __STDC__ )
#define NEEDPROTOS
#endif

#ifdef _SLDAPD_H_	/* server build: no need to use LDAP_CALL stuff */
#ifdef LDAP_CALL
#undef LDAP_CALL
#define LDAP_CALL
#endif
#endif

#ifdef NEEDPROTOS
int slapd_re_init( void );
void slapd_re_lock( void );
int slapd_re_unlock( void );
char * LDAP_CALL slapd_re_comp( char *pat );
int LDAP_CALL slapd_re_exec( char *lp );
void LDAP_CALL slapd_re_modw( char *s );
int LDAP_CALL slapd_re_subs( char *src, char *dst );
#else /* NEEDPROTOS */
int slapd_re_init();
void slapd_re_lock();
int slapd_re_unlock();
char * LDAP_CALL slapd_re_comp();
int LDAP_CALL slapd_re_exec();
void LDAP_CALL slapd_re_modw();
int LDAP_CALL slapd_re_subs();
#endif /* NEEDPROTOS */

#define re_fail( m, p )

#ifdef __cplusplus
}
#endif
#endif /* macintosh or DOS or or _WIN32 or NEED_BSDREGEX */

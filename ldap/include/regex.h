/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(macintosh) || defined(NEED_BSDREGEX)
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

#if !defined(NEEDPROTOS) && defined(__STDC__)
#define NEEDPROTOS
#endif

#ifdef _SLDAPD_H_ /* server build: no need to use LDAP_CALL stuff */
#ifdef LDAP_CALL
#undef LDAP_CALL
#define LDAP_CALL
#endif
#endif

#ifdef NEEDPROTOS
int re_init(void);
void re_lock(void);
int re_unlock(void);
char *re_comp(char *pat);
int re_exec(char *lp);
void re_modw(char *s);
int re_subs(char *src, char *dst);
#else  /* NEEDPROTOS */
int re_init();
void re_lock();
int re_unlock();
char *re_comp();
int re_exec();
void re_modw();
int re_subs();
#endif /* NEEDPROTOS */

#define re_fail(m, p)

#ifdef __cplusplus
}
#endif
#endif /* macintosh or NEED_BSDREGEX */

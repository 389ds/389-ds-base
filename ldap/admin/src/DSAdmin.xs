/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
	This file contains the definitions of C functions callable from perl.
	The perl interface for these functions is found in DSAdmin.pm.
*/

#include "dsalib.h"

#include "nsutils.h"
#include "utf8.h"

/* these are the perl include files needed */
#ifdef __cplusplus
extern "C" {
#endif
#include "EXTERN.h"
/* The next two lines are hacks because someone build perl with gcc which
has this feature call __attribute__ which is not present with sun cc */
#define HASATTRIBUTE
#define __attribute__(_attr_)

#ifdef HPUX11 /* conflict with perl 'struct magic' and hpux 'struct magic' */
#define magic p_magic
#define MAGIC p_MAGIC
#endif /* HPUX */

#include "perl.h"
#include "XSUB.h"
#ifdef __cplusplus
}
#endif


MODULE = DSAdmin  PACKAGE = DSAdmin

PROTOTYPES: DISABLE

SV *
normalizeDN(dn)
	char* dn
	PREINIT:
	char* temp_dn;
	CODE:
	/* duplicate the DN since dn_normalize_convert modifies the argument */
	temp_dn = (char *)malloc(strlen(dn) + 1);
	strcpy(temp_dn, dn);
	ST(0) = sv_newmortal();
	/* dn_normalize_convert returns its argument */
	sv_setpv( ST(0), dn_normalize_convert(temp_dn) );
	free(temp_dn);

SV *
toLocal(s)
	char* s
	PREINIT:
	char* temp_s;
	CODE:
	temp_s = UTF8ToLocal(s);
	ST(0) = sv_newmortal();
	sv_setpv( ST(0), temp_s );
	nsSetupFree(temp_s);

SV *
toUTF8(s)
	char* s
	PREINIT:
	char* temp_s;
	CODE:
	temp_s = localToUTF8(s);
	ST(0) = sv_newmortal();
	sv_setpv( ST(0), temp_s );
	nsSetupFree(temp_s);

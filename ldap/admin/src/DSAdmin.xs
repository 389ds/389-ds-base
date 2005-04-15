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
 * do so, delete this exception statement from your version. 
 * 
 * 
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

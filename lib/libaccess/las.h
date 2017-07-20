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


#ifndef LAS_H
#define LAS_H

#include <libaccess/nserror.h>
#include <libaccess/acl.h>
#include <base/plist.h>

NSPR_BEGIN_EXTERN_C

extern int LASTimeOfDayEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern int LASDayOfWeekEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern int LASIpEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern int LASDnsEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern int LASGroupEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern int LASUserEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
/* MLM - for admin delegation */
extern int LASProgramEval(NSErr_t *errp, char *attribute, CmpOp_t comparator, char *pattern, int *cachable, void **las_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);

extern void LASTimeOfDayFlush(void **cookie);
extern void LASDayOfWeekFlush(void **cookie);
extern void LASIpFlush(void **cookie);
extern void LASDnsFlush(void **cookie);

NSPR_END_EXTERN_C

#endif

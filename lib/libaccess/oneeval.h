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

#ifndef LEVAL_H
#define LEVAL_H

NSPR_BEGIN_EXTERN_C

int
freeLAS(NSErr_t *errp, char *attribute, void **las_cookie);

NSPR_END_EXTERN_C

#endif

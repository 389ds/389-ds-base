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

/* ava.c - routines for dealing with attribute value assertions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

int
get_ava(
    BerElement *ber,
    struct ava *ava)
{
    char *type = NULL;

    if (ber_scanf(ber, "{ao}", &type, &ava->ava_value) == LBER_ERROR) {
        slapi_ch_free_string(&type);
        ava_done(ava);
        slapi_log_err(SLAPI_LOG_ERR, "get_ava", "ber_scanf\n");
        return (LDAP_PROTOCOL_ERROR);
    }
    ava->ava_type = slapi_attr_syntax_normalize(type);
    slapi_ch_free_string(&type);
    ava->ava_private = NULL;

    return (0);
}

void
ava_done(
    struct ava *ava)
{
    slapi_ch_free((void **)&(ava->ava_type));
    slapi_ch_free((void **)&(ava->ava_value.bv_val));
}

int
rdn2ava(
    char *rdn,
    struct ava *ava)
{
    char *s;

    if ((s = strchr(rdn, '=')) == NULL) {
        return (-1);
    }
    *s++ = '\0';

    ava->ava_type = rdn;
    strcpy_unescape_value(s, s);
    ava->ava_value.bv_val = s;
    ava->ava_value.bv_len = strlen(s);
    ava->ava_private = NULL;

    return (0);
}

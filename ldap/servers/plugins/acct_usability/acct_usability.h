/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2011 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Account Usability Control plug-in header file
 */
#include "slapi-plugin.h"
#include "slapi-private.h"

/*
 * Plug-in defines
 */
#define AUC_PLUGIN_SUBSYSTEM "account-usability-plugin"
#define AUC_FEATURE_DESC "Account Usability Control"
#define AUC_PLUGIN_DESC "Account Usability Control plugin"
#define AUC_PREOP_DESC "Account Usability Control preop plugin"

#define AUC_OID "1.3.6.1.4.1.42.2.27.9.5.8"

#define AUC_TAG_AVAILABLE 0x80L     /* context specific + primitive */
#define AUC_TAG_NOT_AVAILABLE 0xA1L /* context specific + constructed + 1 */
#define AUC_TAG_INACTIVE 0x80L      /* context specific + primitive */
#define AUC_TAG_RESET 0x81L         /* context specific + primitive + 1 */
#define AUC_TAG_EXPIRED 0x82L       /* context specific + primitive + 2 */
#define AUC_TAG_GRACE 0x83L         /* context specific + primitive + 3 */
#define AUC_TAG_UNLOCK 0x84L        /* context specific + primitive + 4 */

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

/******************************************************************************
*
*       defines
*
******************************************************************************/
#define SNMP_CONFIG_DN "cn=SNMP,cn=config"
#define SNMP_NAME_ATTR "nsSNMPName"
#define SNMP_DESC_ATTR "nsSNMPDescription"
#define SNMP_ORG_ATTR "nsSNMPOrganization"
#define SNMP_LOC_ATTR "nsSNMPLocation"
#define SNMP_CONTACT_ATTR "nsSNMPContact"

/******************************************************************************
*
*    function prototypes
*
******************************************************************************/

int snmp_collator_start(void);
int snmp_collator_stop(void);
void set_snmp_interaction_row(char *host, int port, int error);
void snmp_collator_update(time_t, void *);
void snmp_thread_counters_cleanup(struct snmp_vars_t *);
void snmp_thread_counters_init(struct snmp_vars_t *);

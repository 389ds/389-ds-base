/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************************************
*
*	function prototypes
*
******************************************************************************/

    int snmp_collator_start();
    int snmp_collator_stop();
    void set_snmp_interaction_row(char *host, int port, int error);
    void snmp_collator_update(time_t, void *);

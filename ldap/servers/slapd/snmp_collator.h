/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

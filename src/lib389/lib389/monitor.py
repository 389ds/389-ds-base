# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Monitor class to display current server performance details
"""

import ldap
from ldap import filter as ldap_filter
from lib389._constants import *


class Monitor(object):
    def __init__(self, conn):
        """@param conn - An instance of DirSrv"""
        self.conn = conn
        self.log = conn.log

    def server(self):
        """
        Show current monitoring information from the server
        """
        monitor_keys = [
            'connection',
            'currentconnections',
            'opscompleted',
            'opsinitiated',
            'threads',
            'totalconnections',
            'version',
        ]
        status = {}
        # Should this be an amalgomation of cn=snmp and cn=monitor?
        # In the future it would make sense perhaps to do this
        monitor_status = self.conn.search_s(DN_MONITOR,
                                            ldap.SCOPE_BASE, '(objectClass=*)')
        # We aren't using this yet, so leave it here for when we expand this 
        # again.
        #snmp_status = self.conn.search_s(DN_MONITOR_SNMP,
        #                                 ldap.SCOPE_BASE, '(objectClass=*)')
        if len(monitor_status) > 0:
            for k in monitor_keys:
                if monitor_status[0].hasAttr(k):
                    status[k] = monitor_status[0].getValues(k)
                else:
                    status[k] = None
        return status

    def backend(self, backend):
        """
        @param backend - The backend DB name to show monitoring details for.

        Show monitoring status for the named backend.
        """
        backend = ldap_filter.escape_filter_chars(backend)
        dn = "cn=%s,%s" % (backend, DN_LDBM)

        # How do we handle errors?
        status = self.conn.search_s(dn, ldap.SCOPE_SUBTREE, '(cn=monitor)')
        if len(status) == 0:
            raise ldap.NO_SUCH_OBJECT
        return status

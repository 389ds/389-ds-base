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
            'currenttime',
        ]
        backend_keys = [
            'dbcachehits',
            'dbcachetries',
            'dbcachehitratio',
            'dbcachepagein',
            'dbcachepageout',
            'dbcacheroevict',
            'dbcacherwevict',
        ]
        status = {}
        # Should this be an amalgomation of cn=snmp and cn=monitor?
        # In the future it would make sense perhaps to do this
        try:
            monitor_status = self.conn.search_s(DN_MONITOR,
                                                ldap.SCOPE_BASE,
                                                '(objectClass=*)',
                                                monitor_keys)

            # We aren't using this yet, so leave it here for now
            # again.
            # snmp_status = self.conn.search_s(DN_MONITOR_SNMP,
            #                                  ldap.SCOPE_BASE,
            #                                  '(objectClass=*)')
            backend_status = self.conn.search_s(DN_MONITOR_LDBM,
                                                ldap.SCOPE_BASE,
                                                '(objectClass=*)',
                                                backend_keys)
        except ldap.LDAPError as e:
            return "Unable to retrieve monitor information: error %s" + str(e)

        status['dn'] = 'cn=monitor'  # Generic DN, but we need a DN

        # There is likely a smarter way to do this on the entry
        if len(monitor_status) == 1:
            for k in monitor_keys:
                status[k] = monitor_status[0].getValues(k)
        else:
            # Error case?
            pass

        if len(backend_status) == 1:
            for k in backend_keys:
                status[k] = backend_status[0].getValues(k)
        else:
            # Error case?
            pass
        return status

    def backend(self, backend):
        """
        @param backend - The backend DB name to show monitoring details for.

        Show monitoring status for the named backend.
        """

        backend_keys = [
            'readonly',
            'entrycachehits',
            'entrycachetries',
            'entrycachehitratio',
            'currententrycachesize',
            'maxentrycachesize',
            'currententrycachecount',
            'maxentrycachecount',
            'dncachehits',
            'dncachetries',
            'dncachehitratio',
            'currentdncachesize',
            'maxdncachesize',
            'currentdncachecount',
            'maxdncachecount',
            'normalizeddncachetries',
            'normalizeddncachehits',
            'normalizeddncachemisses',
            'normalizeddncachehitratio',
            'currentnormalizeddncachesize',
            'maxnormalizeddncachesize',
            'currentnormalizeddncachecount',
        ]

        backend = ldap_filter.escape_filter_chars(backend)
        dn = "cn=%s,%s" % (backend, DN_LDBM)

        # How do we handle errors?
        try:
            backend_status = self.conn.search_s(dn,
                                                ldap.SCOPE_SUBTREE,
                                                '(cn=monitor)',
                                                backend_keys)
        except ldap.LDAPError as e:
            return ('Unable to retrieve backend monitor information: ' +
                    'error %s' + str(e))

        status = {}
        if len(backend_status) == 1:
            status['dn'] = backend_status[0].dn
            for k in backend_keys:
                status[k] = backend_status[0].getValues(k)
        else:
            # Error case?
            pass
        return status

    def backends(self):
        """
        Show monitoring status for all backends on the server.
        """
        # List all backends
        status = {}
        backends = self.conn.backend.list()
        for backend in backends:
            status[backend.cn] = self.backend(backend.cn)
        return status

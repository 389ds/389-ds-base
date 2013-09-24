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
from lib389._mapped_object import DSLdapObject


class Monitor(DSLdapObject):
    """
        Allows reading of cn=monitor for server statistics.
    """

    def __init__(self, instance, dn=None, batch=False):
        super(Monitor, self).__init__(instance=instance, batch=batch)
        self._dn = DN_MONITOR
        self._monitor_keys = [
            'instanceection',
            'currentinstanceections',
            'currentconnections',
            'opscompleted',
            'opsinitiated',
            'threads',
            'totalinstanceections',
            'version',
            'currenttime',
            'connection',
        ]

    def status(self):
        return self.get_attrs_vals(self._monitor_keys)

class MonitorLDBM(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(MonitorLDBM, self).__init__(instance=instance, batch=batch)
        self._dn = DN_MONITOR_LDBM
        self._backend_keys = [
            'dbcachehits',
            'dbcachetries',
            'dbcachehitratio',
            'dbcachepagein',
            'dbcachepageout',
            'dbcacheroevict',
            'dbcacherwevict',
        ]

    def status(self):
        return self.get_attrs_vals(self._backend_keys)

class MonitorBackend(DSLdapObject):
    """
    This is initialised from Backend in backend.py to get the right basedn.
    """

    def __init__(self, instance, dn=None, batch=False):
        super(MonitorBackend, self).__init__(instance=instance, dn=dn, batch=batch)
        self._backend_keys = [
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

    def status(self):
        return self.get_attrs_vals(self._backend_keys)


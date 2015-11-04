"""Monitor class to display current server performance details
"""


import ldap
from ldap import filter as ldap_filter

from lib389._constants import *
from lib389 import Entry

class Monitor(object):
    def __init__(self, conn):
        """@param conn - An instance of DirSrv"""
        self.conn = conn
        self.log = conn.log

    def server(self):
        """
        Show current monitoring information from the server
        """
        # Should this be an amalgomation of cn=snmp and cn=monitor?
        # In the future it would make sense perhaps to do this
        status = self.conn.search_s(DN_MONITOR, ldap.SCOPE_SUBTREE, '(objectClass=*)')
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



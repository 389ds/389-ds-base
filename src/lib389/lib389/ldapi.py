# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject

class LDAPIMapping(DSLdapObject):
    """
    Manage LDAPI mapping entries
    """
    def __init__(self, instance, container_dn, dn=None):
        """
        @param instance - a DirSrv instance
        @param container_dn - the subtree where the mapping entries are located
        """
        super(LDAPIMapping, self).__init__(instance, dn)
        #self._dn = dn
        self._rdn_attribute = 'cn'
        self._filterattrs = ['cn']
        self._must_attributes = ['nsslapd-ldapiusername', 'nsslapd-authenticateasdn']
        self._create_objectclasses = ['top', 'nsLDAPIAuthMap']
        self._container_dn = container_dn

    def create_mapping(self, name, username, ldap_dn):
        """
        @param name - name for the container entry.  This will be the RDN value.
        @param username - the system username
        @param ldap_dn - the LDAP DN that the system user should be mapped to
        """
        self._dn = None  # clear the DN before creating a new mapping
        self.create(basedn=self._container_dn,
                    properties={'cn': name,
                                'nsslapd-ldapiusername': username,
                                'nsslapd-authenticateasdn': ldap_dn})


class LDAPIFixedMapping(DSLdapObject):
    """
    Manage LDAPI mapping entries
    """
    def __init__(self, instance, container_dn, dn=None):
        """
        @param instance - a DirSrv instance
        @param container_dn - the subtree where the mapping entries are located
        """
        super(LDAPIFixedMapping, self).__init__(instance, dn)
        self._dn = dn
        self._rdn_attribute = 'cn'
        self._filterattrs = ['cn']
        self._must_attributes = ['uidNumber', 'gidNumber', 'nsslapd-authenticateasdn']
        self._create_objectclasses = ['top', 'nsLDAPIFixedAuthMap']
        self._container_dn = container_dn

    def create_mapping(self, name, uid, gid, ldap_dn):
        """
        @param name - name for the container entry.  This will be the RDN value.
        @param uid - the system user's uid
        @param gid - the system user's gid
        @param ldap_dn - the LDAP DN that the system user should be mapped to
        """
        self._dn = None  # clear the DN before creating a new mapping
        self.create(basedn=self._container_dn,
                    properties={'cn': name,
                                'uidNumber': uid,
                                'gidNumber': gid,
                                'nsslapd-authenticateasdn': ldap_dn})

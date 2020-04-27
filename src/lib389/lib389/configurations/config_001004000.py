# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from .config import baseconfig, configoperation
from .sample import (
    sampleentries,
    create_base_domain,
    create_base_org,
    create_base_orgunit,
    create_base_cn,
)
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.group import Groups
from lib389.idm.posixgroup import PosixGroups
from lib389.idm.user import nsUserAccounts
from lib389.idm.nscontainer import nsHiddenContainers


class c001004000_sample_entries(sampleentries):
    def __init__(self, instance, basedn):
        super(c001004000_sample_entries, self).__init__(instance, basedn)
        self.description = """Apply sample entries matching the 1.4.0 sample data and access controls"""

    # All checks done, apply!
    def _apply(self):
        suffix_rdn_attr = self._basedn.split('=')[0].lower()
        if suffix_rdn_attr == 'dc':
            suffix_obj = create_base_domain(self._instance, self._basedn)
            aci_vals = ['dc', 'domain']
        elif suffix_rdn_attr == 'o':
            suffix_obj = create_base_org(self._instance, self._basedn)
            aci_vals = ['o', 'organization']
        elif suffix_rdn_attr == 'ou':
            suffix_obj = create_base_orgunit(self._instance, self._basedn)
            aci_vals = ['ou', 'organizationalunit']
        elif suffix_rdn_attr == 'cn':
            suffix_obj = create_base_cn(self._instance, self._basedn)
            aci_vals = ['cn', 'nscontainer']
        else:
            # Unsupported rdn
            raise ValueError("Suffix RDN is not supported for creating sample entries.  Only 'dc', 'o', 'ou', and 'cn' are supported.")

        # Create the base object
        suffix_obj.add('aci', [
            # Allow reading the base domain object
            '(targetattr="' + aci_vals[0] + ' || description || objectClass")(targetfilter="(objectClass=' + aci_vals[1] + ')")(version 3.0; acl "Enable anyone domain read"; allow (read, search, compare)(userdn="ldap:///anyone");)',
            # Allow reading the ou
            '(targetattr="ou || objectClass")(targetfilter="(objectClass=organizationalUnit)")(version 3.0; acl "Enable anyone ou read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        ])

        # Create the 389 service container
        # This could also move to be part of core later ....
        hidden_containers = nsHiddenContainers(self._instance, self._basedn)
        hidden_containers.create(properties={
            'cn': '389_ds_system'
            })

        # Create our ous.
        ous = OrganizationalUnits(self._instance, self._basedn)
        ous.create(properties = {
            'ou': 'groups',
            'aci': [
                # Allow anon partial read
                '(targetattr="cn || member || gidNumber || nsUniqueId || description || objectClass")(targetfilter="(objectClass=groupOfNames)")(version 3.0; acl "Enable anyone group read"; allow (read, search, compare)(userdn="ldap:///anyone");)',
                # Allow group_modify to modify but not create groups
                '(targetattr="member")(targetfilter="(objectClass=groupOfNames)")(version 3.0; acl "Enable group_modify to alter members"; allow (write)(groupdn="ldap:///cn=group_modify,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
                # Allow group_admin to fully manage groups (posix or not).
                '(targetattr="cn || member || gidNumber || description || objectClass")(targetfilter="(objectClass=groupOfNames)")(version 3.0; acl "Enable group_admin to manage groups"; allow (write, add, delete)(groupdn="ldap:///cn=group_admin,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
            ]
        })

        ous.create(properties = {
            'ou': 'people',
            'aci': [
                # allow anon partial read.
                '(targetattr="objectClass || description || nsUniqueId || uid || displayName || loginShell || uidNumber || gidNumber || gecos || homeDirectory || cn || memberOf || mail || nsSshPublicKey || nsAccountLock || userCertificate")(targetfilter="(objectClass=posixaccount)")(version 3.0; acl "Enable anyone user read"; allow (read, search, compare)(userdn="ldap:///anyone");)',
                # allow self partial mod
                '(targetattr="displayName || nsSshPublicKey")(version 3.0; acl "Enable self partial modify"; allow (write)(userdn="ldap:///self");)',
                # Allow self full read
                '(targetattr="legalName || telephoneNumber || mobile")(targetfilter="(objectClass=nsPerson)")(version 3.0; acl "Enable self legalname read"; allow (read, search, compare)(userdn="ldap:///self");)',
                # Allow reading legal name
                '(targetattr="legalName || telephoneNumber")(targetfilter="(objectClass=nsPerson)")(version 3.0; acl "Enable user legalname read"; allow (read, search, compare)(groupdn="ldap:///cn=user_private_read,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
                # These below need READ so they can read userPassword and legalName
                # Allow user admin create mod
                '(targetattr="uid || description || displayName || loginShell || uidNumber || gidNumber || gecos || homeDirectory || cn || memberOf || mail || legalName || telephoneNumber || mobile")(targetfilter="(&(objectClass=nsPerson)(objectClass=nsAccount))")(version 3.0; acl "Enable user admin create"; allow (write, add, delete, read)(groupdn="ldap:///cn=user_admin,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
                # Allow user mod mod only
                '(targetattr="uid || description || displayName || loginShell || uidNumber || gidNumber || gecos || homeDirectory || cn || memberOf || mail || legalName || telephoneNumber || mobile")(targetfilter="(&(objectClass=nsPerson)(objectClass=nsAccount))")(version 3.0; acl "Enable user modify to change users"; allow (write, read)(groupdn="ldap:///cn=user_modify,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
                # Allow user_pw_admin to nsaccountlock and password
                '(targetattr="userPassword || nsAccountLock || userCertificate || nsSshPublicKey")(targetfilter="(objectClass=nsAccount)")(version 3.0; acl "Enable user password reset"; allow (write, read)(groupdn="ldap:///cn=user_passwd_reset,ou=permissions,{BASEDN}");)'.format(BASEDN=self._basedn),
            ]
        })

        ous.create(properties = {
            'ou': 'permissions',
        })

        ous.create(properties = {
            'ou': 'services',
            'aci': [
                # Minimal service read
                '(targetattr="objectClass || description || nsUniqueId || cn || memberOf || nsAccountLock ")(targetfilter="(objectClass=netscapeServer)")(version 3.0; acl "Enable anyone service account read"; allow (read, search, compare)(userdn="ldap:///anyone");)',
            ]
        })

        # Create the demo user
        users = nsUserAccounts(self._instance, self._basedn)
        users.create(properties={
            'uid': 'demo_user',
            'cn': 'Demo User',
            'displayName': 'Demo User',
            'legalName': 'Demo User Name',
            'uidNumber': '99998',
            'gidNumber': '99998',
            'homeDirectory': '/var/empty',
            'loginShell': '/bin/false',
            'nsAccountlock': 'true'
        })

        # Create the demo group
        groups = PosixGroups(self._instance, self._basedn)
        groups.create(properties={
            'cn' : 'demo_group',
            'gidNumber': '99999'
        })

        # Create the permission groups required for the acis
        permissions = Groups(self._instance, self._basedn, rdn='ou=permissions')
        permissions.create(properties={
            'cn': 'group_admin',
        })
        permissions.create(properties={
            'cn': 'group_modify',
        })
        permissions.create(properties={
            'cn': 'user_admin',
        })
        permissions.create(properties={
            'cn': 'user_modify',
        })
        permissions.create(properties={
            'cn': 'user_passwd_reset',
        })
        permissions.create(properties={
            'cn': 'user_private_read',
        })


class c001004000(baseconfig):
    def __init__(self, instance):
        super(c001004000, self).__init__(instance)
        self._operations = [
            # For now this is an empty place holder - likely this
            # will become part of core server.
        ]

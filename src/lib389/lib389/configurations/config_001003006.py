# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from ldap import dn

from .config import baseconfig, configoperation
from .sample import sampleentries

from lib389.idm.domain import Domain
from lib389.idm.organisationalunit import OrganisationalUnits
from lib389.idm.group import UniqueGroups, UniqueGroup

class c001003006_sample_entries(sampleentries):
    def __init__(self, instance, basedn):
        super(c001003006_sample_entries, self).__init__(instance, basedn)
        self.description = """Apply sample entries matching the 1.3.6 sample data and access controls."""

    # All the checks are done, apply them.
    def _apply(self):
        # Create the base domain object
        domain = Domain(self._instance)
        domain._dn = self._basedn
        # Explode the dn to get the first bit.
        avas = dn.str2dn(self._basedn)
        dc_ava = avas[0][0][1]

        domain.create(properties={
            # I think in python 2 this forces unicode return ...
            'dc': dc_ava,
            'description': self._basedn,
            'aci' : '(targetattr ="*")(version 3.0;acl "Directory Administrators Group";allow (all) (groupdn = "ldap:///cn=Directory Administrators, %{BASEDN}");)'.format(BASEDN=self._basedn)
            })
        # Create the OUs
        ous = OrganisationalUnits(self._instance, self._basedn)
        ous.create(properties = {
            'ou': 'Groups',
        })
        ous.create(properties = {
            'ou': 'People',
            'aci' : [
                '(targetattr ="userpassword || telephonenumber || facsimiletelephonenumber")(version 3.0;acl "Allow self entry modification";allow (write)(userdn = "ldap:///self");)',
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Accounting)")(version 3.0;acl "Accounting Managers Group Permissions";allow (write)(groupdn = "ldap:///cn=Accounting Managers,ou=groups,%{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Human Resources)")(version 3.0;acl "HR Group Permissions";allow (write)(groupdn = "ldap:///cn=HR Managers,ou=groups,%{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn ||sn || uid")(targetfilter ="(ou=Product Testing)")(version 3.0;acl "QA Group Permissions";allow (write)(groupdn = "ldap:///cn=QA Managers,ou=groups,%{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Product Development)")(version 3.0;acl "Engineering Group Permissions";allow (write)(groupdn = "ldap:///cn=PD Managers,ou=groups,%{BASEDN}");)'.format(BASEDN=self._basedn),
            ]
        })
        ous.create(properties = {
            'ou': 'Special Users',
            'description' : 'Special Administrative Accounts',
        })
        # Create the groups.
        ugs = UniqueGroups(self._instance, self._basedn)
        ugs.create(properties = {
            'cn': 'Accounting Managers',
            'description': 'People who can manage accounting entries',
            'ou': 'groups',
            'uniqueMember' : self._instance.binddn,
        })
        ugs.create(properties = {
            'cn': 'HR Managers',
            'description': 'People who can manage HR entries',
            'ou': 'groups',
            'uniqueMember' : self._instance.binddn,
        })
        ugs.create(properties = {
            'cn': 'QA Managers',
            'description': 'People who can manage QA entries',
            'ou': 'groups',
            'uniqueMember' : self._instance.binddn,
        })
        ugs.create(properties = {
            'cn': 'PD Managers',
            'description': 'People who can manage engineer entries',
            'ou': 'groups',
            'uniqueMember' : self._instance.binddn,
        })
        # Create the directory Admin group.
        # We can't use the group factory here, as we need a custom DN override.
        da_ug = UniqueGroup(self._instance)
        da_ug._dn = 'cn=Directory Administrators,%s' % self._basedn
        da_ug.create(properties={
            'cn': 'Directory Administrators',
            'uniqueMember' : self._instance.binddn,
        })
        # DONE!

### Operations to be filled in soon!

class c001003006(baseconfig):
    def __init__(self, instance):
        super(c001003006, self).__init__(instance)
        self._operations = [
            # Create our sample entries.
            # op001003006_sample_entries(self._instance),
        ]




# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from .config import baseconfig, configoperation
from .sample import sampleentries, create_base_domain

from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.group import UniqueGroups, UniqueGroup

from lib389.plugins import WhoamiPlugin

class c001003006_sample_entries(sampleentries):
    def __init__(self, instance, basedn):
        super(c001003006_sample_entries, self).__init__(instance, basedn)
        self.description = """Apply sample entries matching the 1.3.6 sample data and access controls."""
        self.version = "c001003006"

    # All the checks are done, apply them.
    def _apply(self):
        # Create the base domain object
        domain = create_base_domain(self._instance, self._basedn)
        domain.add('aci' , '(targetattr ="*")(version 3.0;acl "Directory Administrators Group";allow (all) (groupdn = "ldap:///cn=Directory Administrators,{BASEDN}");)'.format(BASEDN=self._basedn))

        # Create the OUs
        ous = OrganizationalUnits(self._instance, self._basedn)
        ous.create(properties = {
            'ou': 'Groups',
        })
        ous.create(properties = {
            'ou': 'People',
            'aci' : [
                '(targetattr ="userpassword || telephonenumber || facsimiletelephonenumber")(version 3.0;acl "Allow self entry modification";allow (write)(userdn = "ldap:///self");)',
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Accounting)")(version 3.0;acl "Accounting Managers Group Permissions";allow (write)(groupdn = "ldap:///cn=Accounting Managers,ou=groups,{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Human Resources)")(version 3.0;acl "HR Group Permissions";allow (write)(groupdn = "ldap:///cn=HR Managers,ou=groups,{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn ||sn || uid")(targetfilter ="(ou=Product Testing)")(version 3.0;acl "QA Group Permissions";allow (write)(groupdn = "ldap:///cn=QA Managers,ou=groups,{BASEDN}");)'.format(BASEDN=self._basedn),
                '(targetattr !="cn || sn || uid")(targetfilter ="(ou=Product Development)")(version 3.0;acl "Engineering Group Permissions";allow (write)(groupdn = "ldap:///cn=PD Managers,ou=groups,{BASEDN}");)'.format(BASEDN=self._basedn),
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

class c001003006_whoami_plugin(configoperation):
    def __init__(self, instance):
        super(c001003006_whoami_plugin, self).__init__(instance)
        self.install = True
        self.upgrade = True
        self.description = "Enable the Whoami Plugin"

    def _apply(self):
        # This needs to change from create to "ensure"?
        whoami_plugin = WhoamiPlugin(self._instance)
        whoami_plugin.create()

class c001003006(baseconfig):
    def __init__(self, instance):
        super(c001003006, self).__init__(instance)
        self._operations = [
            # Create plugin configs first
            # c001003006_whoami_plugin(self._instance)
            # Create our sample entries.
            # op001003006_sample_entries(self._instance),
        ]


    # We need the following to be created and ENABLED
    # SyntaxValidationPlugin
    # SchemaReloadPlugin
    # StateChangePlugin
    # RolesPlugin
    # ACLPlugin
    # ACLPreoperationPlugin
    # ClassOfServicePlugin
    # ViewsPlugin
    # SevenBitCheckPlugin
    # AccountUsabilityPlugin
    # AutoMembershipPlugin
    # DereferencePlugin
    # HTTPClientPlugin
    # LinkedAttributesPlugin
    # ManagedEntriesPlugin
    # WhoamiPlugin
    # LDBMBackendPlugin
    # ChainingBackendPlugin
    


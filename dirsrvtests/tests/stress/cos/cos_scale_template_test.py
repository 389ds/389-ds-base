# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

from lib389.topologies import topology_st

from lib389.plugins import ClassOfServicePlugin
from lib389.cos import CosIndirectDefinitions, CosTemplates, CosTemplate
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalunit import OrganizationalUnits

from lib389._constants import DEFAULT_SUFFIX

import time

pytestmark = pytest.mark.tier3

# Given this should complete is about 0.005, this is generous.
# For the final test with 20 templates, about 0.02 is an acceptable time.
THRESHOLD = 0.05

class OUCosTemplate(CosTemplate):
    def __init__(self, instance, dn=None):
        """Create a OU specific cos template to replicate a specific user setup.
        This template provides ou attrs onto the target entry.

        :param instance: A dirsrv instance
        :type instance: DirSrv
        :param dn: The dn of the template
        :type dn: str
        """
        super(OUCosTemplate, self).__init__(instance, dn)
        self._rdn_attribute = 'ou'
        self._must_attributes = ['ou']
        self._create_objectclasses = [
            'top',
            'cosTemplate',
            'organizationalUnit',
        ]

class OUCosTemplates(CosTemplates):
    def __init__(self, instance, basedn, rdn=None):
        """Create an OU specific cos templates to replicate a specific use setup.
        This costemplates object allows access to the OUCosTemplate types.

        :param instance: A dirsrv instance
        :type instance: DirSrv
        :param basedn: The basedn of the templates
        :type basedn: str
        :param rdn: The rdn of the templates
        :type rdn: str
        """
        super(OUCosTemplates, self).__init__(instance, basedn, rdn)
        self._objectclasses = [
            'cosTemplate',
            'organizationalUnit',
        ]
        self._filterattrs = ['ou']
        self._childobject = OUCosTemplate

def test_indirect_template_scale(topology_st):
    """Test that cos templates can be added at a reasonable scale

    :id: 7cbcdf22-1f9c-4222-9e76-685fe374fc20
    :steps:
        1. Enable COS plugin
        2. Create the test user
        3. Add an indirect cos template
        4. Add a cos template
        5. Add the user to the cos template and assert it works.
        6. Add 25,000 templates to the database
        7. Search the user. It should not exceed THRESHOLD.
    :expectedresults:
        1. It is enabled.
        2. It is created.
        3. Is is created.
        4. It is created.
        5. It is valid.
        6. They are created.
        7. It is fast.
    """

    cos_plugin = ClassOfServicePlugin(topology_st.standalone)
    cos_plugin.enable()

    topology_st.standalone.restart()

    # Now create, the indirect specifier, and a user to template onto.
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)

    cos_inds = CosIndirectDefinitions(topology_st.standalone, DEFAULT_SUFFIX)
    cos_ind = cos_inds.create(properties={
        'cn' : 'cosIndirectDef',
        'cosIndirectSpecifier': 'seeAlso',
        'cosAttribute': [
            'ou merge-schemes',
            'description merge-schemes',
            'postalCode merge-schemes',
            ],
    })

    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou_temp = ous.create(properties={'ou': 'templates'})
    cos_temps = OUCosTemplates(topology_st.standalone, ou_temp.dn)

    cos_temp_u = cos_temps.create(properties={
        'ou' : 'ou_temp_u',
        'description' : 'desc_temp_u',
        'postalCode': '0'
    })
    # Edit the user to add the seeAlso ...
    user.set('seeAlso', cos_temp_u.dn)

    # Now create 25,0000 templates, they *don't* need to apply to the user though!
    for i in range(1, 25001):
        cos_temp_u = cos_temps.create(properties={
            'ou' : 'ou_temp_%s' % i,
            'description' : 'desc_temp_%s' % i,
            'postalCode': '%s' % i
        })

        if i % 500 == 0:
            start_time = time.monotonic()
            u_search = users.get('testuser')
            attrs = u_search.get_attr_vals_utf8('postalCode')
            end_time = time.monotonic()
            diff_time = end_time - start_time
            assert diff_time < THRESHOLD

        if i == 10000:
            # Now add our user to this template also.
            user.add('seeAlso', cos_temp_u.dn)

            start_time = time.monotonic()
            attrs_after = u_search.get_attr_vals_utf8('postalCode')
            end_time = time.monotonic()
            diff_time = end_time - start_time
            assert(set(attrs) < set(attrs_after))
            assert diff_time < THRESHOLD




# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import ldap
import pytest
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_m2 as topo
from lib389._constants import *

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BINVALUE1 = 'thedeadbeef1'
BINVALUE2 = 'thedeadbeef2'
BINVALUE3 = 'thedeadbeef3'

USER_PROPERTIES = {
    'uid': 'state1usr',
    'cn': 'state1usr',
    'sn': 'state1usr',
    'uidNumber': '1001',
    'gidNumber': '2001',
    'userpassword': PASSWORD,
    'homeDirectory': '/home/testuser'
}


def _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr):
    """Check if list of operational attributes present for a given entry"""

    log.info('Checking if operational attrs vucsn, adcsn and vdcsn present for: {}'.format(tuser))
    entry = topo.ms["supplier1"].search_s(tuser.dn, ldap.SCOPE_BASE, 'objectclass=*',['nscpentrywsi'])
    if oper_attr:
        match = False
        for line in str(entry).split('\n'):
            if attr_name.lower() + ';' in line.lower():
                match = True
                if not 'DELETE' in oper_type:
                    assert any(attr in line for attr in exp_values) and oper_attr in line
                else:
                    assert 'deleted' in line and oper_attr in line

        # If we didn't look at a single attribute then something went wrong
        assert match


@pytest.mark.parametrize("attr_name, attr_value, oper_type, exp_values, oper_attr",
                         [('description', 'Test1usr1', 'ldap.MOD_ADD', ['Test1usr1'], 'vucsn'),
                          ('description', 'Test1usr2', 'ldap.MOD_ADD', ['Test1usr1',
                                                                        'Test1usr2'], 'vucsn'),
                          ('description', 'Test1usr3', 'ldap.MOD_ADD',
                           ['Test1usr1', 'Test1usr2', 'Test1usr3'], 'vucsn'),
                          ('description', 'Test1usr4', 'ldap.MOD_REPLACE', ['Test1usr4'],
                           'adcsn'),
                          ('description', 'Test1usr4', 'ldap.MOD_DELETE', [], 'vdcsn')])
def test_check_desc_attr_state(topo, attr_name, attr_value, oper_type, exp_values, oper_attr):
    """Modify user's description attribute and check if description attribute is
    added/modified/deleted and operational attributes vucsn, adcsn and vdcsn are present.

    :id: f0830538-02cf-11e9-8be0-8c16451d917b
    :parametrized: yes
    :setup: Replication with two suppliers.
    :steps: 1. Add user to Supplier1 without description attribute.
            2. Add description attribute to user.
            3. Check if only one description attribute exist.
            4. Check if operational attribute vucsn exist.
            5. Add second description attribute to user.
            6. Check if two description attributes exist.
            7. Check if operational attribute vucsn exist.
            8. Add third description attribute to user.
            9. Check if three description attributes exist.
            10. Check if operational attribute vucsn exist.
            11. Replace description attribute for the user.
            12. Check if only one description attribute exist.
            13. Check if operational attribute adcsn exist.
            14. Delete description attribute for the user.
            15. Check if no description attribute exist.
            16. Check if no operational attribute vdcsn exist.
    :expectedresults:
            1. Add user to M1 should PASS.
            2. Adding description attribute should PASS
            3. Only one description attribute should be present.
            4. Vucsn attribute should be present.
            5. Adding a new description attribute should PASS
            6. Two description attribute should be present.
            7. Vucsn attribute should be present.
            8. Adding a new description attribute should PASS
            9. Three description attribute should be present.
            10. Vucsn attribute should be present.
            11. Replacing new description attribute should PASS
            12. Only one description attribute should be present.
            13. Adcsn attribute should be present.
            14. Deleting description attribute should PASS
            15. No description attribute should be present.
            16. Vdcsn attribute should be present.
    """

    test_entry = 'state1test'
    log.info('Add user: {}'.format(test_entry))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    try:
        tuser = users.get(test_entry)
    except ldap.NO_SUCH_OBJECT:
        USER_PROPERTIES.update(dict.fromkeys(['uid', 'cn'], test_entry))
        tuser = users.create(properties=USER_PROPERTIES)
    tuser.set(attr_name, attr_value, eval(oper_type))
    log.info('Check if list of description attrs present for: {}'.format(test_entry))
    assert sorted([i.decode() for i in tuser.get_attr_vals(attr_name)]) == sorted(exp_values)

    log.info('Checking for operational attributes')
    _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr)


@pytest.mark.parametrize("attr_name, attr_value, oper_type, exp_values, oper_attr",
                         [('cn', 'TestCN1', 'ldap.MOD_ADD', ['TestCN1', 'TestCNusr1'], 'vucsn'),
                          ('cn', 'TestCN2', 'ldap.MOD_ADD', ['TestCN1',
                                                             'TestCN2', 'TestCNusr1'], 'vucsn'),
                          ('cn', 'TestnewCN3', 'ldap.MOD_REPLACE', ['TestnewCN3'], 'adcsn'),
                          ('cn', 'TestnewCN3', 'ldap.MOD_DELETE', None, None)])
def test_check_cn_attr_state(topo, attr_name, attr_value, oper_type, exp_values, oper_attr):
    """Modify user's cn attribute and check if cn attribute is added/modified/deleted and
    operational attributes vucsn, adcsn and vdcsn are present.

    :id: 19614bae-02d0-11e9-a295-8c16451d917b
    :parametrized: yes
    :setup: Replication with two suppliers.
    :steps: 1. Add user to Supplier1 with cn attribute.
            2. Add a new cn attribute to user.
            3. Check if two cn attributes exist.
            4. Check if operational attribute vucsn exist for each cn attribute.
            5. Add a new cn attribute to user.
            6. Check if three cn attributes exist.
            7. Check if operational attribute vucsn exist for each cn attribute.
            8. Replace cn attribute for the user.
            9. Check if only one cn attribute exist.
            10. Check if operational attribute adcsn exist.
            11. Delete cn attribute from user and check if it fails.
    :expectedresults:
            1. Add user to M1 should PASS.
            2. Adding a new cn attribute should PASS
            3. Two cn attribute should be present.
            4. Vucsn attribute should be present.
            5. Adding a new cn attribute should PASS
            6. Three cn attribute should be present.
            7. Vucsn attribute should be present.
            8. Replacing new cn attribute should PASS
            9. Only one cn attribute should be present.
            10. Operational attribute adcsn should be present.
            11. Deleting cn attribute should fail with ObjectClass violation error.
    """

    test_entry = 'TestCNusr1'
    log.info('Add user: {}'.format(test_entry))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    try:
        tuser = users.get(test_entry)
    except ldap.NO_SUCH_OBJECT:
        USER_PROPERTIES.update(dict.fromkeys(['uid', 'cn'], test_entry))
        tuser = users.create(properties=USER_PROPERTIES)

    if 'MOD_DELETE' in oper_type:
        with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
            tuser.set(attr_name, attr_value, eval(oper_type))
    else:
        tuser.set(attr_name, attr_value, eval(oper_type))
        log.info('Check if list of cn attrs present for: {}'.format(test_entry))
        assert sorted([i.decode() for i in tuser.get_attr_vals(attr_name)]) == sorted(exp_values)
    log.info('Checking for operational attributes')
    _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr)


@pytest.mark.parametrize("attr_name, attr_value, oper_type, exp_values, oper_attr",
                         [('preferredlanguage', 'Chinese', 'ldap.MOD_REPLACE', ['Chinese'],
                           'vucsn'),
                          ('preferredlanguage', 'French', 'ldap.MOD_ADD', None, None),
                          ('preferredlanguage', 'German', 'ldap.MOD_REPLACE', ['German'], 'adcsn'),
                          ('preferredlanguage', 'German', 'ldap.MOD_DELETE', [], 'vdcsn')])
def test_check_single_value_attr_state(topo, attr_name, attr_value, oper_type,
                                       exp_values, oper_attr):
    """Modify user's preferredlanguage attribute and check if preferredlanguage attribute is
    added/modified/deleted and operational attributes vucsn, adcsn and vdcsn are present.

    :id: 22fd645e-02d0-11e9-a9e4-8c16451d917b
    :parametrized: yes
    :setup: Replication with two suppliers.
    :steps: 1. Add user to Supplier1 without preferredlanguage attribute.
            2. Add a new preferredlanguage attribute to user.
            3. Check if one preferredlanguage attributes exist.
            4. Check if operational attribute vucsn exist.
            5. Add a new preferredlanguage attribute for the user and check if its rejected.
            6. Replace preferredlanguage attribute for the user.
            7. Check if only one preferredlanguage attribute exist.
            8. Check if operational attribute adcsn exist with preferredlanguage.
    :expectedresults:
            1. Add user to M1 should PASS.
            2. Adding a new preferredlanguage attribute should PASS
            3. Only one preferredlanguage attribute should be present.
            4. Vucsn attribute should be present.
            5. Adding a new preferredlanguage should fail with ObjectClass violation error.
            6. Replace preferredlanguage should PASS.
            7. Only one preferredlanguage attribute should be present.
            8. Operational attribute adcsn should be present with preferredlanguage.
    """

    test_entry = 'Langusr1'
    log.info('Add user: {}'.format(test_entry))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    try:
        tuser = users.get(test_entry)
    except ldap.NO_SUCH_OBJECT:
        USER_PROPERTIES.update(dict.fromkeys(['uid', 'cn'], test_entry))
        tuser = users.create(properties=USER_PROPERTIES)

    if 'MOD_ADD' in oper_type:
        with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
            tuser.set(attr_name, attr_value, eval(oper_type))
    else:
        tuser.set(attr_name, attr_value, eval(oper_type))
        log.info('Check if list of cn attrs present for: {}'.format(test_entry))
        assert sorted([i.decode() for i in tuser.get_attr_vals(attr_name)]) == sorted(exp_values)
    log.info('Checking for operational attributes')
    _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr)


@pytest.mark.parametrize("attr_name, attr_value, oper_type, exp_values, oper_attr",
                         [('roomnumber;office', 'Tower1', 'ldap.MOD_ADD', ['Tower1'], 'vucsn'),
                          ('roomnumber;office', 'Tower2', 'ldap.MOD_ADD', ['Tower1', 'Tower2'],
                           'vucsn'),
                          ('roomnumber;office', 'Tower3', 'ldap.MOD_ADD', ['Tower1', 'Tower2',
                                                                           'Tower3'], 'vucsn'),
                          ('roomnumber;office', 'Tower4', 'ldap.MOD_REPLACE', ['Tower4'], 'adcsn'),
                          ('roomnumber;office', 'Tower4', 'ldap.MOD_DELETE', [], 'vucsn')])
def test_check_subtype_attr_state(topo, attr_name, attr_value, oper_type, exp_values, oper_attr):
    """Modify user's roomnumber;office attribute subtype and check if roomnumber;office attribute
    is added/modified/deleted and operational attributes vucsn, adcsn and vdcsn are present.

    :id: 29ab87a4-02d0-11e9-b104-8c16451d917b
    :parametrized: yes
    :setup: Replication with two suppliers.
    :steps: 1. Add user to Supplier1 without roomnumber;office attribute.
            2. Add roomnumber;office attribute to user.
            3. Check if only one roomnumber;office attribute exist.
            4. Check if operational attribute vucsn exist.
            5. Add second roomnumber;office attribute to user.
            6. Check if two roomnumber;office attributes exist.
            7. Check if operational attribute vucsn exist.
            8. Add third roomnumber;office attribute to user.
            9. Check if three roomnumber;office attributes exist.
            10. Check if operational attribute vucsn exist.
            11. Replace roomnumber;office attribute for the user.
            12. Check if only one roomnumber;office attribute exist.
            13. Check if operational attribute adcsn exist.
            14. Delete roomnumber;office attribute for the user.
            15. Check if no roomnumber;office attribute exist.
            16. Check if no operational attribute vdcsn exist.
    :expectedresults:
            1. Add user to M1 should PASS.
            2. Adding roomnumber;office attribute should PASS
            3. Only one roomnumber;office attribute should be present.
            4. Vucsn attribute should be present.
            5. Adding a new roomnumber;office attribute should PASS
            6. Two roomnumber;office attribute should be present.
            7. Vucsn attribute should be present.
            8. Adding a new roomnumber;office attribute should PASS
            9. Three roomnumber;office attribute should be present.
            10. Vucsn attribute should be present.
            11. Replacing new roomnumber;office attribute should PASS
            12. Only one roomnumber;office attribute should be present.
            13. Adcsn attribute should be present.
            14. Deleting roomnumber;office attribute should PASS
            15. No roomnumber;office attribute should be present.
            16. Vdcsn attribute should be present.
    """

    test_entry = 'roomoffice1usr'
    log.info('Add user: {}'.format(test_entry))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    try:
        tuser = users.get(test_entry)
    except ldap.NO_SUCH_OBJECT:
        USER_PROPERTIES.update(dict.fromkeys(['uid', 'cn'], test_entry))
        tuser = users.create(properties=USER_PROPERTIES)

    tuser.set(attr_name, attr_value, eval(oper_type))
    log.info('Check if list of roomnumber;office attributes are present for a given entry')
    assert sorted([i.decode() for i in tuser.get_attr_vals(attr_name)]) == sorted(exp_values)
    log.info('Checking if operational attributes are present for cn')
    _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr)


@pytest.mark.parametrize("attr_name, attr_value, oper_type, exp_values, oper_attr",
                         [('jpegphoto', BINVALUE1, 'ldap.MOD_ADD', [BINVALUE1], 'vucsn'),
                          ('jpegphoto', BINVALUE2, 'ldap.MOD_ADD', [BINVALUE1, BINVALUE2],
                           'vucsn'),
                          ('jpegphoto', BINVALUE3, 'ldap.MOD_ADD', [BINVALUE1, BINVALUE2,
                                                                    BINVALUE3], 'vucsn'),
                          ('jpegphoto', BINVALUE2, 'ldap.MOD_REPLACE', [BINVALUE2], 'adcsn'),
                          ('jpegphoto', BINVALUE2, 'ldap.MOD_DELETE', [], 'vdcsn')])
def test_check_jpeg_attr_state(topo, attr_name, attr_value, oper_type, exp_values, oper_attr):
    """Modify user's jpegphoto attribute and check if jpegphoto attribute is added/modified/deleted
    and operational attributes vucsn, adcsn and vdcsn are present.

    :id: 312ac0d0-02d0-11e9-9d34-8c16451d917b
    :parametrized: yes
    :setup: Replication with two suppliers.
    :steps: 1. Add user to Supplier1 without jpegphoto attribute.
            2. Add jpegphoto attribute to user.
            3. Check if only one jpegphoto attribute exist.
            4. Check if operational attribute vucsn exist.
            5. Add second jpegphoto attribute to user.
            6. Check if two jpegphoto attributes exist.
            7. Check if operational attribute vucsn exist.
            8. Add third jpegphoto attribute to user.
            9. Check if three jpegphoto attributes exist.
            10. Check if operational attribute vucsn exist.
            11. Replace jpegphoto attribute for the user.
            12. Check if only one jpegphoto attribute exist.
            13. Check if operational attribute adcsn exist.
            14. Delete jpegphoto attribute for the user.
            15. Check if no jpegphoto attribute exist.
            16. Check if no operational attribute vdcsn exist.
    :expectedresults:
            1. Add user to M1 should PASS.
            2. Adding jpegphoto attribute should PASS
            3. Only one jpegphoto attribute should be present.
            4. Vucsn attribute should be present.
            5. Adding a new jpegphoto attribute should PASS
            6. Two jpegphoto attribute should be present.
            7. Vucsn attribute should be present.
            8. Adding a new jpegphoto attribute should PASS
            9. Three jpegphoto attribute should be present.
            10. Vucsn attribute should be present.
            11. Replacing new jpegphoto attribute should PASS
            12. Only one jpegphoto attribute should be present.
            13. Adcsn attribute should be present.
            14. Deleting jpegphoto attribute should PASS
            15. No jpegphoto attribute should be present.
            16. Vdcsn attribute should be present.
    """

    test_entry = 'testJpeg1usr'
    log.info('Add user: {}'.format(test_entry))
    users = UserAccounts(topo.ms['supplier1'], DEFAULT_SUFFIX)
    try:
        tuser = users.get(test_entry)
    except ldap.NO_SUCH_OBJECT:
        USER_PROPERTIES.update(dict.fromkeys(['uid', 'cn'], test_entry))
        tuser = users.create(properties=USER_PROPERTIES)

    tuser.set(attr_name, attr_value, eval(oper_type))
    log.info('Check if list of jpeg attributes are present for a given entry')
    assert sorted([i.decode() for i in tuser.get_attr_vals(attr_name)]) == sorted(exp_values)
    log.info('Checking if operational attributes are present for cn')
    _check_user_oper_attrs(topo, tuser, attr_name, attr_value, oper_type, exp_values, oper_attr)


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

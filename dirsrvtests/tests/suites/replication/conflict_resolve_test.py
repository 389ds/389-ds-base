# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import logging
import ldap
import pytest
import re
from itertools import permutations
from lib389._constants import *
from lib389.idm.nscontainer import nsContainers
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.group import Groups
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import ReplicationManager, Replicas
from lib389.agreement import Agreements
from lib389.plugins import MemberOfPlugin
from lib389.dirsrv_log import DirsrvErrorLog

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _create_user(users, user_num, group_num=2000, sleep=False):
    """Creates user entry"""

    user = users.create_test_user(user_num, group_num)
    if sleep:
        time.sleep(1)
    return user


def _rename_user(users, user_num, new_num, sleep=False):
    """Rename user entry"""

    assert user_num != new_num, "New user number should not be the same as the old one"

    user = users.get('test_user_{}'.format(user_num))
    user.rename('uid=test_user_{}'.format(new_num))
    if sleep:
        time.sleep(1)


def _modify_user(users, user_num, sleep=False):
    """Modify user entry"""

    user = users.get('test_user_{}'.format(user_num))
    user.replace("homeDirectory", "/home/test_user0{}".format(user_num))
    if sleep:
        time.sleep(1)
    time.sleep(1)


def _delete_user(users, user_num, sleep=False):
    """Delete user entry"""

    user = users.get('test_user_{}'.format(user_num))
    user.delete()
    if sleep:
        time.sleep(1)
    time.sleep(1)


def _create_group(groups, num, member, sleep=False):
    """Creates group entry"""

    group_props = {'cn': 'test_group_{}'.format(num),
                   'member': member}
    group = groups.create(properties=group_props)
    if sleep:
        time.sleep(1)
    return group


def _delete_group(groups, num, sleep=False):
    """Delete group entry"""

    group = groups.get('test_group_{}'.format(num))
    group.delete()
    if sleep:
        time.sleep(1)


def _create_container(inst, dn, name, sleep=False):
    """Creates container entry"""

    conts = nsContainers(inst, dn)
    cont = conts.create(properties={'cn': name})
    if sleep:
        time.sleep(1)
    return cont


def _delete_container(cont, sleep=False):
    """Deletes container entry"""

    cont.delete()
    if sleep:
        time.sleep(1)


def _test_base(topology):
    """Add test container for entries, enable plugin logging,
    audit log, error log for replica and access log for internal
    """

    M1 = topology.ms["supplier1"]

    conts = nsContainers(M1, SUFFIX)
    base_m2 = conts.ensure_state(properties={'cn': 'test_container'})

    for inst in topology:
        inst.config.loglevel([ErrorLog.DEFAULT, ErrorLog.REPLICA], service='error')
        inst.config.loglevel([AccessLog.DEFAULT, AccessLog.INTERNAL], service='access')
        inst.config.set('nsslapd-plugin-logging', 'on')
        inst.config.enable_log('audit')
        inst.restart()

    return base_m2


def _dump_logs(topology):
    """ Logs instances error logs"""
    for inst in topology:
        errlog = DirsrvErrorLog(inst)
        log.info(f'{inst.serverid} errorlog:')
        for l in errlog.readlines():
            log.info(l.strip())


def _delete_test_base(inst, base_m2_dn):
    """Delete test container with entries and entry conflicts"""

    try:
        ents = inst.search_s(base_m2_dn, ldap.SCOPE_SUBTREE, filterstr="(|(objectclass=*)(objectclass=ldapsubentry))")
        for ent in sorted(ents, key=lambda e: len(e.dn), reverse=True):
            log.debug("Delete entry children {}".format(ent.dn))
            try:
                inst.delete_ext_s(ent.dn)
            except ldap.NO_SUCH_OBJECT:  # For the case with objectclass: glue entries
                pass
    except ldap.NO_SUCH_OBJECT:  # Subtree is already removed.
        pass


def _resume_agmts(inst):
    """Resume all agreements in the instance"""

    replicas = Replicas(inst)
    replica = replicas.get(DEFAULT_SUFFIX)
    for agreement in replica.get_agreements().list():
        agreement.resume()


@pytest.fixture
def base_m2(topology_m2, request):
    tb = _test_base(topology_m2)

    def fin():
        if not DEBUGGING:
            _delete_test_base(topology_m2.ms["supplier1"], tb.dn)
            _delete_test_base(topology_m2.ms["supplier2"], tb.dn)
            # Replication may break while deleting the container because naming
            # conflict entries still exists on the other side
            # Note IMHO there a bug in the entryrdn handling of replicated delete operation
            # ( children naming conflict or glue entries older than the parent delete operation should
            #   should be deleted when the parent is deleted )
            # So let restarts the agmt once everything is deleted.
            topology_m2.pause_all_replicas()
            topology_m2.resume_all_replicas()

    request.addfinalizer(fin)

    return tb


@pytest.fixture
def base_m3(topology_m3, request):
    tb = _test_base(topology_m3)

    def fin():
        if not DEBUGGING:
            _delete_test_base(topology_m3.ms["supplier1"], tb.dn)
    request.addfinalizer(fin)

    return tb


class TestTwoSuppliers:
    def test_add_modrdn(self, topology_m2, base_m2):
        """Check that conflict properly resolved for create - modrdn operations

        :id: 77f09b18-03d1-45da-940b-1ad2c2908ebb
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Add five users to m1 and wait for replication to happen
            2. Pause replication
            3. Create an entry on m1 and m2
            4. Create an entry on m1 and rename on m2
            5. Rename an entry on m1 and create on m2
            6. Rename an entry on m1 and rename on m2
            7. Rename an entry on m1 and rename on m2. Use different entries
               but rename them to the same entry
            8. Resume replication
            9. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
        """

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        test_users_m2 = UserAccounts(M2, base_m2.dn, rdn=None)
        repl = ReplicationManager(SUFFIX)

        for user_num in range(1000, 1005):
            _create_user(test_users_m1, user_num)

        repl.test_replication(M1, M2)
        topology_m2.pause_all_replicas()

        log.info("Test create - modrdn")
        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _rename_user(test_users_m2, 1000, user_num, sleep=True)

        user_num += 1
        _rename_user(test_users_m1, 1001, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)

        user_num += 1
        _rename_user(test_users_m1, 1002, user_num, sleep=True)
        _rename_user(test_users_m2, 1002, user_num, sleep=True)

        user_num += 1
        _rename_user(test_users_m1, 1003, user_num, sleep=True)
        _rename_user(test_users_m2, 1004, user_num)

        topology_m2.resume_all_replicas()

        repl.test_replication_topology(topology_m2)

        user_dns_m1 = [user.dn for user in test_users_m1.list()]
        user_dns_m2 = [user.dn for user in test_users_m2.list()]
        assert set(user_dns_m1) == set(user_dns_m2)

    def test_complex_add_modify_modrdn_delete(self, topology_m2, base_m2):
        """Check that conflict properly resolved for complex operations
        which involve add, modify, modrdn and delete

        :id: 77f09b18-03d1-45da-940b-1ad2c2908eb1
        :customerscenario: True
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Add ten users to m1 and wait for replication to happen
            2. Pause replication
            3. Test add-del on m1 and add on m2
            4. Test add-mod on m1 and add on m2
            5. Test add-modrdn on m1 and add on m2
            6. Test multiple add, modrdn
            7. Test Add-del on both suppliers
            8. Test modrdn-modrdn
            9. Test modrdn-del
            10. Resume replication
            11. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
            9. It should pass
            10. It should pass
            11. It should pass
        """

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]

        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        test_users_m2 = UserAccounts(M2, base_m2.dn, rdn=None)
        repl = ReplicationManager(SUFFIX)

        for user_num in range(1100, 1110):
            _create_user(test_users_m1, user_num)

        repl.test_replication(M1, M2)
        topology_m2.pause_all_replicas()

        log.info("Test add-del on M1 and add on M2")
        user_num += 1
        _create_user(test_users_m1, user_num)
        _delete_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)
        _delete_user(test_users_m1, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m2, user_num, sleep=True)
        _create_user(test_users_m1, user_num)
        _delete_user(test_users_m1, user_num)

        log.info("Test add-mod on M1 and add on M2")
        user_num += 1
        _create_user(test_users_m1, user_num)
        _modify_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)
        _modify_user(test_users_m1, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m2, user_num, sleep=True)
        _create_user(test_users_m1, user_num)
        _modify_user(test_users_m1, user_num)

        log.info("Test add-modrdn on M1 and add on M2")
        user_num += 1
        _create_user(test_users_m1, user_num)
        _rename_user(test_users_m1, user_num, user_num+20, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)

        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)
        _rename_user(test_users_m1, user_num, user_num+20, sleep=True)

        user_num += 1
        _create_user(test_users_m2, user_num, sleep=True)
        _create_user(test_users_m1, user_num)
        _rename_user(test_users_m1, user_num, user_num+20)

        log.info("Test multiple add, modrdn")
        user_num += 1
        _create_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num, sleep=True)
        _rename_user(test_users_m1, user_num, user_num+20)
        _create_user(test_users_m1, user_num, sleep=True)
        _modify_user(test_users_m2, user_num, sleep=True)

        log.info("Add - del on both suppliers")
        user_num += 1
        _create_user(test_users_m1, user_num)
        _delete_user(test_users_m1, user_num, sleep=True)
        _create_user(test_users_m2, user_num)
        _delete_user(test_users_m2, user_num, sleep=True)

        log.info("Test modrdn - modrdn")
        user_num += 1
        _rename_user(test_users_m1, 1109, 1129, sleep=True)
        _rename_user(test_users_m2, 1109, 1129, sleep=True)

        log.info("Test modrdn - del")
        user_num += 1
        _rename_user(test_users_m1, 1100, 1120, sleep=True)
        _delete_user(test_users_m2, 1100)

        user_num += 1
        _delete_user(test_users_m2, 1101, sleep=True)
        _rename_user(test_users_m1, 1101, 1121)

        topology_m2.resume_all_replicas()

        repl.test_replication_topology(topology_m2)

        user_dns_m1 = [user.dn for user in test_users_m1.list()]
        user_dns_m2 = [user.dn for user in test_users_m2.list()]
        assert set(user_dns_m1) == set(user_dns_m2)

    def test_memberof_groups(self, topology_m2, base_m2):
        """Check that conflict properly resolved for operations
        with memberOf and groups

        :id: 77f09b18-03d1-45da-940b-1ad2c2908eb3
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Enable memberOf plugin
            2. Add 30 users to m1 and wait for replication to happen
            3. Pause replication
            4. Create a group on m1 and m2
            5. Create a group on m1 and m2, delete from m1
            6. Create a group on m1, delete from m1, and create on m2,
            7. Create a group on m2 and m1, delete from m1
            8. Create two different groups on m2
            9. Resume replication
            10. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
            9. It should pass
            10. It should pass
        """

        pytest.xfail("Issue 49591 - work in progress")

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        test_groups_m1 = Groups(M1, base_m2.dn, rdn=None)
        test_groups_m2 = Groups(M2, base_m2.dn, rdn=None)

        repl = ReplicationManager(SUFFIX)

        for inst in topology_m2.ms.values():
            memberof = MemberOfPlugin(inst)
            memberof.enable()
            agmt = Agreements(inst).list()[0]
            agmt.replace_many(('nsDS5ReplicatedAttributeListTotal',
                               '(objectclass=*) $ EXCLUDE '),
                              ('nsDS5ReplicatedAttributeList',
                               '(objectclass=*) $ EXCLUDE memberOf'))
            inst.restart()
        user_dns = []
        for user_num in range(10):
            user_trio = []
            for num in range(0, 30, 10):
                user = _create_user(test_users_m1, 1200 + user_num + num)
                user_trio.append(user.dn)
            user_dns.append(user_trio)

        repl.test_replication(M1, M2)
        topology_m2.pause_all_replicas()

        log.info("Check a simple conflict")
        group_num = 0
        _create_group(test_groups_m1, group_num, user_dns[group_num], sleep=True)
        _create_group(test_groups_m2, group_num, user_dns[group_num], sleep=True)

        log.info("Check a add - del")
        group_num += 1
        _create_group(test_groups_m1, group_num, user_dns[group_num], sleep=True)
        _create_group(test_groups_m2, group_num, user_dns[group_num], sleep=True)
        _delete_group(test_groups_m1, group_num)

        group_num += 1
        _create_group(test_groups_m1, group_num, user_dns[group_num])
        _delete_group(test_groups_m1, group_num, sleep=True)
        _create_group(test_groups_m2, group_num, user_dns[group_num])

        group_num += 1
        _create_group(test_groups_m2, group_num, user_dns[group_num], sleep=True)
        _create_group(test_groups_m1, group_num, user_dns[group_num])
        _delete_group(test_groups_m1, group_num, sleep=True)

        group_num += 1
        _create_group(test_groups_m2, group_num, user_dns[group_num])
        group_num += 1
        _create_group(test_groups_m2, group_num, user_dns[group_num])

        topology_m2.resume_all_replicas()

        repl.test_replication_topology(topology_m2)

        group_dns_m1 = [group.dn for group in test_groups_m1.list()]
        group_dns_m2 = [group.dn for group in test_groups_m2.list()]
        assert set(group_dns_m1) == set(group_dns_m2)

    def test_managed_entries(self, topology_m2):
        """Check that conflict properly resolved for operations
        with managed entries

        :id: 77f09b18-03d1-45da-940b-1ad2c2908eb4
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Create ou=managed_users and ou=managed_groups under test container
            2. Configure managed entries plugin and add a template to test container
            3. Add a user to m1 and wait for replication to happen
            4. Pause replication
            5. Create a user on m1 and m2 with a same group ID on both supplier
            6. Create a user on m1 and m2 with a different group ID on both supplier
            7. Resume replication
            8. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
        """

        pytest.xfail("Issue 49591 - work in progress")

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        repl = ReplicationManager(SUFFIX)

        ous = OrganizationalUnits(M1, DEFAULT_SUFFIX)
        ou_people = ous.create(properties={'ou': 'managed_people'})
        ou_groups = ous.create(properties={'ou': 'managed_groups'})

        test_users_m1 = UserAccounts(M1, DEFAULT_SUFFIX, rdn='ou={}'.format(ou_people.rdn))
        test_users_m2 = UserAccounts(M2, DEFAULT_SUFFIX, rdn='ou={}'.format(ou_people.rdn))

        # TODO: Refactor ManagedPlugin class  functionality (also add configs and templates)
        conts = nsContainers(M1, SUFFIX)
        template = conts.create(properties={
                                 'objectclass': 'top mepTemplateEntry extensibleObject'.split(),
                                 'cn': 'MEP Template',
                                 'mepRDNAttr': 'cn',
                                 'mepStaticAttr': ['objectclass: posixGroup', 'objectclass: extensibleObject'],
                                 'mepMappedAttr': ['cn: $uid', 'uid: $cn', 'gidNumber: $uidNumber']
                                })
        repl.test_replication(M1, M2)

        for inst in topology_m2.ms.values():
            conts = nsContainers(inst, "cn={},{}".format(PLUGIN_MANAGED_ENTRY, DN_PLUGIN))
            conts.create(properties={'objectclass': 'top extensibleObject'.split(),
                                     'cn': 'config',
                                     'originScope': ou_people.dn,
                                     'originFilter': 'objectclass=posixAccount',
                                     'managedBase': ou_groups.dn,
                                     'managedTemplate': template.dn})
            inst.restart()

        _create_user(test_users_m1, 1, 1)

        topology_m2.pause_all_replicas()

        _create_user(test_users_m1, 2, 2, sleep=True)
        _create_user(test_users_m2, 2, 2, sleep=True)

        _create_user(test_users_m1, 3, 3, sleep=True)
        _create_user(test_users_m2, 3, 33)

        topology_m2.resume_all_replicas()

        repl.test_replication_topology(topology_m2)

        user_dns_m1 = [user.dn for user in test_users_m1.list()]
        user_dns_m2 = [user.dn for user in test_users_m2.list()]
        assert set(user_dns_m1) == set(user_dns_m2)

    def test_nested_entries_with_children(self, topology_m2, base_m2):
        """Check that conflict properly resolved for operations
        with nested entries with children

        :id: 77f09b18-03d1-45da-940b-1ad2c2908eb5
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Add 15 containers to m1 and wait for replication to happen
            2. Pause replication
            3. Create parent-child on supplier2 and supplier1
            4. Create parent-child on supplier1 and supplier2
            5. Create parent-child on supplier1 and supplier2 different child rdn
            6. Create parent-child on supplier1 and delete parent on supplier2
            7. Create parent on supplier1, delete it and parent-child on supplier2, delete them
            8. Create parent on supplier1, delete it and parent-two children on supplier2
            9. Create parent-two children on supplier1 and parent-child on supplier2, delete them
            10. Create three subsets inside existing container entry, applying only part of changes on m2
            11. Create more combinations of the subset with parent-child on m1 and parent on m2
            12. Delete container on m1, modify user1 on m1, create parent on m2 and modify user2 on m2
            13. Resume replication
            14. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
            9. It should pass
            10. It should pass
            11. It should pass
            12. It should pass
            13. It should pass
            14. It should pass
        """

        pytest.xfail("Issue 49591 - work in progress")

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        repl = ReplicationManager(SUFFIX)
        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        test_users_m2 = UserAccounts(M2, base_m2.dn, rdn=None)
        _create_user(test_users_m1, 4000)
        _create_user(test_users_m1, 4001)

        cont_list = []
        for num in range(15):
            cont = _create_container(M1, base_m2.dn, 'sub{}'.format(num))
            cont_list.append(cont)

        repl.test_replication(M1, M2)

        topology_m2.pause_all_replicas()

        log.info("Create parent-child on supplier2 and supplier1")
        _create_container(M2, base_m2.dn, 'p0', sleep=True)
        cont_p = _create_container(M1, base_m2.dn, 'p0', sleep=True)
        _create_container(M1, cont_p.dn, 'c0', sleep=True)
        _create_container(M2, cont_p.dn, 'c0', sleep=True)

        log.info("Create parent-child on supplier1 and supplier2")
        cont_p = _create_container(M1, base_m2.dn, 'p1', sleep=True)
        _create_container(M2, base_m2.dn, 'p1', sleep=True)
        _create_container(M1, cont_p.dn, 'c1', sleep=True)
        _create_container(M2, cont_p.dn, 'c1', sleep=True)

        log.info("Create parent-child on supplier1 and supplier2 different child rdn")
        cont_p = _create_container(M1, base_m2.dn, 'p2', sleep=True)
        _create_container(M2, base_m2.dn, 'p2', sleep=True)
        _create_container(M1, cont_p.dn, 'c2', sleep=True)
        _create_container(M2, cont_p.dn, 'c3', sleep=True)

        log.info("Create parent-child on supplier1 and delete parent on supplier2")
        cont_num = 0
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)
        _create_container(M1, cont_p_m1.dn, 'c0', sleep=True)
        _delete_container(cont_p_m2)

        cont_num += 1
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0', sleep=True)
        _delete_container(cont_p_m2, sleep=True)

        log.info("Create parent on supplier1, delete it and parent-child on supplier2, delete them")
        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _delete_container(cont_p_m1, sleep=True)

        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2)
        _delete_container(cont_p_m2)

        cont_num += 1
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2)
        _delete_container(cont_p_m2, sleep=True)

        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _delete_container(cont_p_m1)

        log.info("Create parent on supplier1, delete it and parent-two children on supplier2")
        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _delete_container(cont_p_m1, sleep=True)

        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        _create_container(M2, cont_p_m2.dn, 'c0')
        _create_container(M2, cont_p_m2.dn, 'c1')

        cont_num += 1
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        _create_container(M2, cont_p_m2.dn, 'c0')
        _create_container(M2, cont_p_m2.dn, 'c1', sleep=True)

        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _delete_container(cont_p_m1, sleep=True)

        log.info("Create parent-two children on supplier1 and parent-child on supplier2, delete them")
        cont_num += 1
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2)
        _delete_container(cont_p_m2, sleep=True)

        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0')
        _create_container(M1, cont_p_m1.dn, 'c1')

        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0')
        _create_container(M1, cont_p_m1.dn, 'c1', sleep=True)

        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2)
        _delete_container(cont_p_m2, sleep=True)

        log.info("Create three subsets inside existing container entry, applying only part of changes on m2")
        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0')
        _create_container(M1, cont_p_m1.dn, 'c1', sleep=True)
        _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)

        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0')
        _create_container(M1, cont_p_m1.dn, 'c1', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        _create_container(M2, cont_p_m2.dn, 'c0', sleep=True)

        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0')
        _create_container(M1, cont_p_m1.dn, 'c0')
        _create_container(M1, cont_p_m1.dn, 'c1', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0')
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2, sleep=True)

        log.info("Create more combinations of the subset with parent-child on m1 and parent on m2")
        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)
        _delete_container(cont_p_m1, sleep=True)
        cont_c_m2 = _create_container(M2, cont_p_m2.dn, 'c0')
        _delete_container(cont_c_m2)
        _delete_container(cont_p_m2, sleep=True)

        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)
        _delete_container(cont_p_m1, sleep=True)
        _create_container(M2, cont_p_m2.dn, 'c0', sleep=True)

        cont_num += 1
        cont_p_m1 = _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_p_m2 = _create_container(M2, cont_list[cont_num].dn, 'p0', sleep=True)
        cont_c_m1 = _create_container(M1, cont_p_m1.dn, 'c0', sleep=True)
        _create_container(M2, cont_p_m2.dn, 'c0', sleep=True)
        _delete_container(cont_c_m1, sleep=True)
        _create_container(M2, cont_p_m2.dn, 'c1', sleep=True)
        _delete_container(cont_p_m1, sleep=True)

        log.info("Delete container on m1, modify user1 on m1, create parent on m2 and modify user2 on m2")
        cont_num += 1
        _delete_container(cont_list[cont_num])
        _modify_user(test_users_m1, 4000, sleep=True)
        _create_container(M2, cont_list[cont_num].dn, 'p0')
        _modify_user(test_users_m2, 4001)

        topology_m2.resume_all_replicas()

        repl.test_replication_topology(topology_m2, timeout=60)

        conts_dns = {}
        for num in range(1, 3):
            inst = topology_m2.ms["supplier{}".format(num)]
            conts_dns[inst.serverid] = []
            conts = nsContainers(inst, base_m2.dn)
            for cont in conts.list():
                conts_p = nsContainers(inst, cont.dn)
                for cont_p in conts_p.list():
                    conts_c = nsContainers(inst, cont_p.dn)
                    conts_dns[inst.serverid].extend([cont_c.dn for cont_c in conts_c.list()])
                conts_dns[inst.serverid].extend([cont_p.dn for cont_p in conts_p.list()])
            conts_dns[inst.serverid].extend([cont.dn for cont in conts.list()])

        assert set(conts_dns[M1.serverid]) == set(conts_dns[M2.serverid])

        user_dns_m1 = [user.dn for user in test_users_m1.list()]
        user_dns_m2 = [user.dn for user in test_users_m2.list()]
        assert set(user_dns_m1) == set(user_dns_m2)

    def test_conflict_attribute_multi_valued(self, topology_m2, base_m2):
        """A RDN attribute being multi-valued, checks that after several operations
           MODRDN and MOD_REPL its RDN values are the same on both servers

        :id: 225b3522-8ed7-4256-96f9-5fab9b7044a5
        :setup: Two supplier replication,
                audit log, error log for replica and access log for internal
        :steps:
            1. Create a test entry uid=user_test_1000,...
            2. Pause all replication agreements
            3. On M1 rename it into uid=foo1,...
            4. On M2 rename it into uid=foo2,...
            5. On M1 MOD_REPL uid:foo1
            6. Resume all replication agreements
            7. Check that entry on M1 has uid=foo1, foo2
            8. Check that entry on M2 has uid=foo1, foo2
            9. Check that entry on M1 and M2 has the same uid values
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
            9. It should pass
        """

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        repl = ReplicationManager(SUFFIX)

        # add a test user
        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        user_1 = test_users_m1.create_test_user(uid=1000)
        test_users_m2 = UserAccount(M2, user_1.dn)
        # Waiting fo the user to be replicated
        for i in range(0,60):
            time.sleep(1)
            if test_users_m2.exists():
                break
        try:
            assert(test_users_m2.exists())
        except AssertionError as e:
            _dump_logs(topology_m2)
            raise e from None

        # Stop replication agreements
        topology_m2.pause_all_replicas()

        # On M1 rename test entry in uid=foo1
        original_dn = user_1.dn
        user_1.rename('uid=foo1')
        time.sleep(1)

        # On M2 rename test entry in uid=foo2
        M2.rename_s(original_dn, 'uid=foo2')
        time.sleep(2)

        # on M1 MOD_REPL uid into foo1
        user_1.replace('uid', 'foo1')

        # resume replication agreements
        topology_m2.resume_all_replicas()
        repl.test_replication_topology(topology_m2)

        # check that on M1, the entry 'uid' has two values 'foo1' and 'foo2'
        final_dn = re.sub('^.*1000,', 'uid=foo2,', original_dn)
        final_user_m1 = UserAccount(M1, final_dn)
        for val in final_user_m1.get_attr_vals_utf8('uid'):
            log.info("Check %s is on M1" % val)
            assert(val in ['foo1', 'foo2'])

        # check that on M2, the entry 'uid' has two values 'foo1' and 'foo2'
        final_user_m2 = UserAccount(M2, final_dn)
        for val in final_user_m2.get_attr_vals_utf8('uid'):
            log.info("Check %s is on M1" % val)
            assert(val in ['foo1', 'foo2'])

        # check that the entry have the same uid values
        for val in final_user_m1.get_attr_vals_utf8('uid'):
            log.info("Check M1.uid %s is also on M2" % val)
            assert(val in final_user_m2.get_attr_vals_utf8('uid'))

        for val in final_user_m2.get_attr_vals_utf8('uid'):
            log.info("Check M2.uid %s is also on M1" % val)
            assert(val in final_user_m1.get_attr_vals_utf8('uid'))

    def test_conflict_attribute_single_valued(self, topology_m2, base_m2):
        """A RDN attribute being signle-valued, checks that after several operations
           MODRDN and MOD_REPL its RDN values are the same on both servers

        :id: c38ae613-5d1e-47cf-b051-c7284e64b817
        :setup: Two supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Create a test entry uid=user_test_1000,...
            2. Pause all replication agreements
            3. On M1 rename it into employeenumber=foo1,...
            4. On M2 rename it into employeenumber=foo2,...
            5. On M1 MOD_REPL employeenumber:foo1
            6. Resume all replication agreements
            7. Check that entry on M1 has employeenumber=foo1
            8. Check that entry on M2 has employeenumber=foo1
            9. Check that entry on M1 and M2 has the same employeenumber values
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
            9. It should pass
        """

        M1 = topology_m2.ms["supplier1"]
        M2 = topology_m2.ms["supplier2"]
        repl = ReplicationManager(SUFFIX)

        # add a test user with a dummy 'uid' extra value because modrdn removes
        # uid that conflict with 'account' objectclass
        test_users_m1 = UserAccounts(M1, base_m2.dn, rdn=None)
        user_1 = test_users_m1.create_test_user(uid=1000)
        user_1.add('objectclass', 'extensibleobject')
        user_1.add('uid', 'dummy')
        test_users_m2 = UserAccount(M2, user_1.dn)

        # Waiting fo the user to be replicated
        for i in range(0,60):
            time.sleep(1)
            if test_users_m2.exists():
                break
        try:
            assert(test_users_m2.exists())
        except AssertionError as e:
            _dump_logs(topology_m2)
            raise e from None

        # Stop replication agreements
        topology_m2.pause_all_replicas()

        # On M1 rename test entry in employeenumber=foo1
        original_dn = user_1.dn
        user_1.rename('employeenumber=foo1')
        time.sleep(1)

        # On M2 rename test entry in employeenumber=foo2
        M2.rename_s(original_dn, 'employeenumber=foo2')
        time.sleep(2)

        # on M1 MOD_REPL uid into foo1
        user_1.replace('employeenumber', 'foo1')

        # resume replication agreements
        topology_m2.resume_all_replicas()
        repl.test_replication_topology(topology_m2)

        # check that on M1, the entry 'employeenumber' has value 'foo1'
        final_dn = re.sub('^.*1000,', 'employeenumber=foo2,', original_dn)
        final_user_m1 = UserAccount(M1, final_dn)
        for val in final_user_m1.get_attr_vals_utf8('employeenumber'):
            log.info("Check %s is on M1" % val)
            assert(val in ['foo1'])

        # check that on M2, the entry 'employeenumber' has values 'foo1'
        final_user_m2 = UserAccount(M2, final_dn)
        for val in final_user_m2.get_attr_vals_utf8('employeenumber'):
            log.info("Check %s is on M2" % val)
            assert(val in ['foo1'])

        # check that the entry have the same uid values
        for val in final_user_m1.get_attr_vals_utf8('employeenumber'):
            log.info("Check M1.uid %s is also on M2" % val)
            assert(val in final_user_m2.get_attr_vals_utf8('employeenumber'))

        for val in final_user_m2.get_attr_vals_utf8('employeenumber'):
            log.info("Check M2.uid %s is also on M1" % val)
            assert(val in final_user_m1.get_attr_vals_utf8('employeenumber'))

class TestThreeSuppliers:
    def test_nested_entries(self, topology_m3, base_m3):
        """Check that conflict properly resolved for operations
        with nested entries with children

        :id: 77f09b18-03d1-45da-940b-1ad2c2908eb6
        :setup: Three supplier replication, test container for entries, enable plugin logging,
                audit log, error log for replica and access log for internal
        :steps:
            1. Add 15 containers to m1 and wait for replication to happen
            2. Pause replication
            3. Create two child entries under each of two entries
            4. Create three child entries under each of three entries
            5. Create two parents on m1 and m2, then on m1 - create a child and delete one parent,
               on m2 - delete one parent and create a child
            6. Test a few more parent-child combinations with three instances
            7. Resume replication
            8. Check that the entries on both suppliers are the same and replication is working
        :expectedresults:
            1. It should pass
            2. It should pass
            3. It should pass
            4. It should pass
            5. It should pass
            6. It should pass
            7. It should pass
            8. It should pass
        """

        pytest.xfail("Issue 49591 - work in progress")

        M1 = topology_m3.ms["supplier1"]
        M2 = topology_m3.ms["supplier2"]
        M3 = topology_m3.ms["supplier3"]
        repl = ReplicationManager(SUFFIX)

        cont_list = []
        for num in range(11):
            cont = _create_container(M1, base_m3.dn, 'sub{}'.format(num))
            cont_list.append(cont)

        repl.test_replication(M1, M2)
        repl.test_replication(M1, M3)

        topology_m3.pause_all_replicas()

        log.info("Create two child entries under each of two entries")
        cont_num = -1
        for num in range(2):
            cont_num += 1
            _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
            _create_container(M2, cont_list[cont_num].dn, 'p1', sleep=True)

        log.info("Create three child entries under each of three entries")
        for num in range(3):
            cont_num += 1
            _create_container(M1, cont_list[cont_num].dn, 'p0', sleep=True)
            _create_container(M2, cont_list[cont_num].dn, 'p1', sleep=True)
            _create_container(M3, cont_list[cont_num].dn, 'p2', sleep=True)

        log.info("Create two parents on m1 and m2, then on m1 - create a child and delete one parent,"
                 "on m2 - delete one parent and create a child")
        for inst1, inst2 in ((M1, M2), (M2, M1)):
            cont_num += 1
            cont_p_m1_1 = _create_container(inst1, cont_list[cont_num].dn, 'p0')
            cont_p_m1_2 = _create_container(inst1, cont_list[cont_num].dn, 'p1', sleep=True)
            cont_p_m2_1 = _create_container(inst2, cont_list[cont_num].dn, 'p0')
            cont_p_m2_2 = _create_container(inst2, cont_list[cont_num].dn, 'p1', sleep=True)
            _create_container(inst1, cont_p_m1_1.dn, 'c0', sleep=True)
            _delete_container(cont_p_m2_1, sleep=True)
            _delete_container(cont_p_m1_2, sleep=True)
            _create_container(inst2, cont_p_m2_2.dn, 'c0', sleep=True)

        log.info("Test a few more parent-child combinations on three instances")
        for inst1, inst2, inst3 in ((M1, M2, M3), (M2, M1, M3), (M3, M1, M2)):
            cont_num += 1
            cont_p_m1 = _create_container(inst1, cont_list[cont_num].dn, 'p0')
            _delete_container(cont_p_m1, sleep=True)

            cont_p_m2 = _create_container(inst2, cont_list[cont_num].dn, 'p0')
            cont_c_m2 = _create_container(inst2, cont_p_m2.dn, 'c0')
            _delete_container(cont_c_m2)
            _delete_container(cont_p_m2, sleep=True)

            cont_p_m3 = _create_container(inst3, cont_list[cont_num].dn, 'p0')
            _create_container(inst3, cont_p_m3.dn, 'c0')
            _create_container(inst3, cont_p_m3.dn, 'c1', sleep=True)

        topology_m3.resume_all_replicas()

        repl.test_replication_topology(topology_m3)

        conts_dns = {}
        for num in range(1, 4):
            inst = topology_m3.ms["supplier{}".format(num)]
            conts_dns[inst.serverid] = []
            conts = nsContainers(inst, base_m3.dn)
            for cont in conts.list():
                conts_p = nsContainers(inst, cont.dn)
                for cont_p in conts_p.list():
                    conts_c = nsContainers(inst, cont_p.dn)
                    conts_dns[inst.serverid].extend([cont_c.dn for cont_c in conts_c.list()])
                conts_dns[inst.serverid].extend([cont_p.dn for cont_p in conts_p.list()])
            conts_dns[inst.serverid].extend([cont.dn for cont in conts.list()])

        for conts1, conts2 in permutations(conts_dns.values(), 2):
            assert set(conts1) == set(conts2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

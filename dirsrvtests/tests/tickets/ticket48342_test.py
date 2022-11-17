# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m3

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, PLUGIN_DNA

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PEOPLE_OU = 'people'
PEOPLE_DN = "ou=%s,%s" % (PEOPLE_OU, SUFFIX)
MAX_ACCOUNTS = 5


def _dna_config(server, nextValue=500, maxValue=510):
    log.info("Add dna plugin config entry...%s" % server)

    try:
        server.add_s(Entry(('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config', {
            'objectclass': 'top dnaPluginConfig'.split(),
            'dnaType': 'description',
            'dnaMagicRegen': '-1',
            'dnaFilter': '(objectclass=posixAccount)',
            'dnaScope': 'ou=people,%s' % SUFFIX,
            'dnaNextValue': str(nextValue),
            'dnaMaxValue': str(nextValue + maxValue),
            'dnaSharedCfgDN': 'ou=ranges,%s' % SUFFIX
        })))

    except ldap.LDAPError as e:
        log.error('Failed to add DNA config entry: error ' + e.args[0]['desc'])
        assert False

    log.info("Enable the DNA plugin...")
    try:
        server.plugins.enable(name=PLUGIN_DNA)
    except e:
        log.error("Failed to enable DNA Plugin: error " + e.args[0]['desc'])
        assert False

    log.info("Restarting the server...")
    server.stop(timeout=120)
    time.sleep(1)
    server.start(timeout=120)
    time.sleep(3)


def test_ticket4026(topology_m3):
    """Write your replication testcase here.

    To access each DirSrv instance use:  topology_m3.ms["supplier1"], topology_m3.ms["supplier2"],
        ..., topology_m3.hub1, ..., topology_m3.consumer1, ...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """

    try:
        topology_m3.ms["supplier1"].add_s(Entry((PEOPLE_DN, {
            'objectclass': "top extensibleObject".split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass

    topology_m3.ms["supplier1"].add_s(Entry(('ou=ranges,' + SUFFIX, {
        'objectclass': 'top organizationalunit'.split(),
        'ou': 'ranges'
    })))
    for cpt in range(MAX_ACCOUNTS):
        name = "user%d" % (cpt)
        topology_m3.ms["supplier1"].add_s(Entry(("uid=%s,%s" % (name, PEOPLE_DN), {
            'objectclass': 'top posixAccount extensibleObject'.split(),
            'uid': name,
            'cn': name,
            'uidNumber': '1',
            'gidNumber': '1',
            'homeDirectory': '/home/%s' % name
        })))

    # make supplier3 having more free slots that supplier2
    # so supplier1 will contact supplier3
    _dna_config(topology_m3.ms["supplier1"], nextValue=100, maxValue=10)
    _dna_config(topology_m3.ms["supplier2"], nextValue=200, maxValue=10)
    _dna_config(topology_m3.ms["supplier3"], nextValue=300, maxValue=3000)

    # Turn on lots of error logging now.

    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'16384')]
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '1')]
    topology_m3.ms["supplier1"].modify_s('cn=config', mod)
    topology_m3.ms["supplier2"].modify_s('cn=config', mod)
    topology_m3.ms["supplier3"].modify_s('cn=config', mod)

    # We need to wait for the event in dna.c to fire to start the servers
    # see dna.c line 899
    time.sleep(60)

    # add on supplier1 users with description DNA
    for cpt in range(10):
        name = "user_with_desc1_%d" % (cpt)
        topology_m3.ms["supplier1"].add_s(Entry(("uid=%s,%s" % (name, PEOPLE_DN), {
            'objectclass': 'top posixAccount extensibleObject'.split(),
            'uid': name,
            'cn': name,
            'description': '-1',
            'uidNumber': '1',
            'gidNumber': '1',
            'homeDirectory': '/home/%s' % name
        })))
    # give time to negociate supplier1 <--> supplier3
    time.sleep(10)
    # add on supplier1 users with description DNA
    for cpt in range(11, 20):
        name = "user_with_desc1_%d" % (cpt)
        topology_m3.ms["supplier1"].add_s(Entry(("uid=%s,%s" % (name, PEOPLE_DN), {
            'objectclass': 'top posixAccount extensibleObject'.split(),
            'uid': name,
            'cn': name,
            'description': '-1',
            'uidNumber': '1',
            'gidNumber': '1',
            'homeDirectory': '/home/%s' % name
        })))
    log.info('Test complete')
    # add on supplier1 users with description DNA
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'16384')]
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '1')]
    topology_m3.ms["supplier1"].modify_s('cn=config', mod)
    topology_m3.ms["supplier2"].modify_s('cn=config', mod)
    topology_m3.ms["supplier3"].modify_s('cn=config', mod)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

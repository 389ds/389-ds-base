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
from lib389.topologies import topology_m2

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)
# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.5'), reason="Not implemented")]



def _create_user(inst, idnum):
    inst.add_s(Entry(
        ('uid=user%s,ou=People,%s' % (idnum, DEFAULT_SUFFIX), {
            'objectClass': 'top account posixAccount'.split(' '),
            'cn': 'user',
            'uid': 'user%s' % idnum,
            'homeDirectory': '/home/user%s' % idnum,
            'loginShell': '/bin/nologin',
            'gidNumber': '-1',
            'uidNumber': '-1',
        })
    ))


def test_ticket48916(topology_m2):
    """
    https://bugzilla.redhat.com/show_bug.cgi?id=1353629

    This is an issue with ID exhaustion in DNA causing a crash.

    To access each DirSrv instance use:  topology_m2.ms["supplier1"], topology_m2.ms["supplier2"],
        ..., topology_m2.hub1, ..., topology_m2.consumer1,...


    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    # Enable the plugin on both servers

    dna_m1 = topology_m2.ms["supplier1"].plugins.get('Distributed Numeric Assignment Plugin')
    dna_m2 = topology_m2.ms["supplier2"].plugins.get('Distributed Numeric Assignment Plugin')

    # Configure it
    # Create the container for the ranges to go into.

    topology_m2.ms["supplier1"].add_s(Entry(
        ('ou=Ranges,%s' % DEFAULT_SUFFIX, {
            'objectClass': 'top organizationalUnit'.split(' '),
            'ou': 'Ranges',
        })
    ))

    # Create the dnaAdmin?

    # For now we just pinch the dn from the dna_m* types, and add the relevant child config
    # but in the future, this could be a better plugin template type from lib389

    config_dn = dna_m1.dn

    topology_m2.ms["supplier1"].add_s(Entry(
        ('cn=uids,%s' % config_dn, {
            'objectClass': 'top dnaPluginConfig'.split(' '),
            'cn': 'uids',
            'dnatype': 'uidNumber gidNumber'.split(' '),
            'dnafilter': '(objectclass=posixAccount)',
            'dnascope': '%s' % DEFAULT_SUFFIX,
            'dnaNextValue': '1',
            'dnaMaxValue': '50',
            'dnasharedcfgdn': 'ou=Ranges,%s' % DEFAULT_SUFFIX,
            'dnaThreshold': '0',
            'dnaRangeRequestTimeout': '60',
            'dnaMagicRegen': '-1',
            'dnaRemoteBindDN': 'uid=dnaAdmin,ou=People,%s' % DEFAULT_SUFFIX,
            'dnaRemoteBindCred': 'secret123',
            'dnaNextRange': '80-90'
        })
    ))

    topology_m2.ms["supplier2"].add_s(Entry(
        ('cn=uids,%s' % config_dn, {
            'objectClass': 'top dnaPluginConfig'.split(' '),
            'cn': 'uids',
            'dnatype': 'uidNumber gidNumber'.split(' '),
            'dnafilter': '(objectclass=posixAccount)',
            'dnascope': '%s' % DEFAULT_SUFFIX,
            'dnaNextValue': '61',
            'dnaMaxValue': '70',
            'dnasharedcfgdn': 'ou=Ranges,%s' % DEFAULT_SUFFIX,
            'dnaThreshold': '2',
            'dnaRangeRequestTimeout': '60',
            'dnaMagicRegen': '-1',
            'dnaRemoteBindDN': 'uid=dnaAdmin,ou=People,%s' % DEFAULT_SUFFIX,
            'dnaRemoteBindCred': 'secret123',
        })
    ))

    # Enable the plugins
    dna_m1.enable()
    dna_m2.enable()

    # Restart the instances
    topology_m2.ms["supplier1"].restart(60)
    topology_m2.ms["supplier2"].restart(60)

    # Wait for a replication .....
    time.sleep(40)

    # Allocate the 10 members to exhaust

    for i in range(1, 11):
        _create_user(topology_m2.ms["supplier2"], i)

    # Allocate the 11th
    _create_user(topology_m2.ms["supplier2"], 11)

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

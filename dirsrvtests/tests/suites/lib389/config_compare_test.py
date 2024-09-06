# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest

from contextlib import suppress
from lib389.topologies import topology_i2
from lib389.config import Config
from lib389.dseldif import DSEldif

pytestmark = pytest.mark.tier1

def test_config_compare(topology_i2):
    """
    Compare test between cn=config of two different Directory Server intance.

    :id: 7b3e17d6-41ca-4926-bc3b-8173dd912a61

    :setup: two isolated directory servers

    :steps: 1. Compare if cn=config is the same

    :expectedresults: 1. It should be the same (excluding unique id attrs)
    """
    st1_config = topology_i2.ins.get('standalone1').config
    st2_config = topology_i2.ins.get('standalone2').config
    # 'nsslapd-port' attribute is expected to be same in cn=config comparison,
    # but they are different in our testing environment
    # as we are using 2 DS instances running, both running simultaneously.
    # Hence explicitly adding 'nsslapd-port' to compare_exclude.
    st1_config._compare_exclude.append('nsslapd-port')
    st2_config._compare_exclude.append('nsslapd-port')
    st1_config._compare_exclude.append('nsslapd-secureport')
    st2_config._compare_exclude.append('nsslapd-secureport')
    st1_config._compare_exclude.append('nsslapd-ldapssotoken-secret')
    st2_config._compare_exclude.append('nsslapd-ldapssotoken-secret')

    assert Config.compare(st1_config, st2_config)


@pytest.fixture(scope="function")
def save_dse(topology_i2, request):
    """
    Stop standalone1 instance and save its dse.ldif then restore things at teardown.
    """
    inst = topology_i2.ins.get('standalone1')
    inst.stop()
    dse_ldif = DSEldif(inst)

    def fin():
        dse_ldif._update()
        inst.start()

    request.addfinalizer(fin)


def get_db_lib(inst):
    """
    Clear cache and returns inst.get_db_lib()
    """
    with suppress(AttributeError):
        del inst._db_lib
    return inst.get_db_lib()


def test_get_db_lib(topology_i2, save_dse):
    """
    Check that get_db_lib() returns the configured database type.

    :id: 04205590-6c70-11ef-bfae-083a88554478

    :setup: two isolated directory servers. standalone1 is stopped.

    :steps: 1. Configure standalone1 with bdb
            2. Check that test_get_db_lib() returns bdb 
            3. Start standalone1 instance
            4. Check that test_get_db_lib() returns bdb 
            5. Stop standalone1 instance
            6. Configure standalone1 with mdb
            7. Check that test_get_db_lib() returns mdb 
            8. Start standalone1 instance
            9. Check that test_get_db_lib() returns mdb 

    :expectedresults: 1. Success
                      2. Success
                      3. Success
                      4. Success
                      5. Success
                      6. Success
                      7. Success
                      8. Success
                      9. Success
    """

    inst = topology_i2.ins.get('standalone1')
    dse_ldif = DSEldif(inst)
    becfgdn = 'cn=config,cn=ldbm database,cn=plugins,cn=config'
    becfgattr = 'nsslapd-backend-implement'

    # Set db type to: bdb
    becfgval = 'bdb'
    dse_ldif.replace(becfgdn, becfgattr, becfgval)
    assert get_db_lib(inst) == becfgval
    inst.start()
    assert get_db_lib(inst) == becfgval
    inst.stop()

    # Set db type to: mdb
    becfgval = 'mdb'
    dse_ldif.replace(becfgdn, becfgattr, becfgval)
    assert get_db_lib(inst) == becfgval
    inst.start()
    assert get_db_lib(inst) == becfgval


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

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

from lib389.topologies import topology_i2
from lib389.config import Config

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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

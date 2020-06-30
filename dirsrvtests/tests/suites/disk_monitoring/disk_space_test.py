# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import pytest
from lib389.monitor import MonitorDiskSpace
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

def test_basic(topo):
    """Test that the cn=disk space,cn=monitor gives at least one value

    :id: f1962762-2c6c-4e50-97af-a00012a7486d
    :setup: Standalone
    :steps:
        1. Get cn=disk space,cn=monitor entry
        2. Check it has at least one dsDisk attribute
        3. Check dsDisk attribute has the partition and sizes
        4. Check the numbers are valid integers
    :expectedresults:
        1. It should succeed
        2. It should succeed
        3. It should succeed
        4. It should succeed
    """

    inst = topo.standalone

    # Turn off disk monitoring
    disk_space_mon = MonitorDiskSpace(inst)
    disk_str = disk_space_mon.get_disks()[0]

    inst.log.info('Check that "partition", "size", "used", "available", "use%" words are present in the string')
    words = ["partition", "size", "used", "available", "use%"]
    assert all(map(lambda word: word in disk_str, words))

    inst.log.info("Check that the sizes are numbers")
    for word in words[1:]:
        number = disk_str.split(f'{word}="')[1].split('"')[0]
        try:
            int(number)
        except ValueError:
            raise ValueError(f'A "{word}" value is not a number')

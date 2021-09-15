# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.cli_conf.monitor import db_monitor
from lib389.cli_base import FakeArgs

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

OUTPUT_NO_INDEXES = [
    'DB Monitor Report',
    'Database Cache:',
    'Cache Hit Ratio:',
    'Free Space:',
    'Free Percentage:',
    'RO Page Drops:',
    'Pages In:',
    'Pages Out:',
    'Normalized DN Cache:',
    'Cache Hit Ratio:',
    'Free Space:',
    'Free Percentage:',
    'DN Count:',
    'Evictions:',
    'Backends:',
    'dc=example,dc=com (userRoot):',
    'Entry Cache Hit Ratio:',
    'Entry Cache Count:',
    'Entry Cache Free Space:',
    'Entry Cache Free Percentage:',
    'Entry Cache Average Size:',
    'DN Cache Hit Ratio:',
    'DN Cache Count:',
    'DN Cache Free Space:',
    'DN Cache Free Percentage:',
    'DN Cache Average Size:'
    ]

OUTPUT_INDEXES = [
    'DB Monitor Report',
    'Database Cache:',
    'Cache Hit Ratio:',
    'Free Space:',
    'Free Percentage:',
    'RO Page Drops:',
    'Pages In:',
    'Pages Out:',
    'Normalized DN Cache:',
    'Cache Hit Ratio:',
    'Free Space:',
    'Free Percentage:',
    'DN Count:',
    'Evictions:',
    'Backends:',
    'dc=example,dc=com (userRoot):',
    'Entry Cache Hit Ratio:',
    'Entry Cache Count:',
    'Entry Cache Free Space:',
    'Entry Cache Free Percentage:',
    'Entry Cache Average Size:',
    'DN Cache Hit Ratio:',
    'DN Cache Count:',
    'DN Cache Free Space:',
    'DN Cache Free Percentage:',
    'DN Cache Average Size:',
    'Indexes:',
    'Index:      aci.db',
    'Cache Hit:',
    'Cache Miss:',
    'Page In:',
    'Page Out:',
    'Index:      id2entry.db',
    'Index:      objectclass.db',
    'Index:      entryrdn.db'
    ]

JSON_OUTPUT = [
    'date',
    'dbcache',
    'hit_ratio',
    'free',
    'free_percentage',
    'roevicts',
    'pagein',
    'pageout',
    'ndncache',
    'hit_ratio',
    'free',
    'free_percentage',
    'count',
    'evictions',
    'backends',
    'userRoot',
    '"suffix": "dc=example,dc=com"',
    'entry_cache_count',
    'entry_cache_free',
    'entry_cache_free_percentage',
    'entry_cache_size',
    'entry_cache_hit_ratio',
    'dn_cache_count',
    'dn_cache_free',
    'dn_cache_free_percentage',
    'dn_cache_size',
    'dn_cache_hit_ratio',
    'indexes',
    'name',
    'objectclass.db',
    'cachehit',
    'cachemiss',
    'pagein',
    'pageout',
    'entryrdn.db',
    'aci.db',
    'id2entry.db'
    ]


def clear_log(inst):
    log.info('Clear the log')
    inst.logcap.flush()


@pytest.mark.ds50545
@pytest.mark.bz1795943
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsconf_dbmon(topology_st):
    """Test dbmon tool, that was ported from legacy tools to dsconf

    :id: 4d584ba9-12a9-4e90-ba9a-7e103affdac5
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Run dbmon without --indexes
         3. Run dbmon with --indexes
         4. Run dbmon with --json
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone

    args = FakeArgs()
    args.backends = DEFAULT_BENAME
    args.indexes = False
    args.json = False

    log.info('Sanity check for syntax')
    db_monitor(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    for item in OUTPUT_NO_INDEXES:
        assert topology_st.logcap.contains(item)

    clear_log(topology_st)

    log.info('Sanity check for --indexes output')
    args.indexes = True
    db_monitor(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    for index_item in OUTPUT_INDEXES:
        assert topology_st.logcap.contains(index_item)

    clear_log(topology_st)

    log.info('Sanity check for --json output')
    args.json = True
    db_monitor(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    for json_item in JSON_OUTPUT:
        assert topology_st.logcap.contains(json_item)

    clear_log(topology_st)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

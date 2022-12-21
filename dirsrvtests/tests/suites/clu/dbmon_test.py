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
import json
import glob

from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st, topology_m2
from lib389.cli_conf.monitor import db_monitor
from lib389.monitor import MonitorLDBM
from lib389.cli_base import FakeArgs, LogCapture
from lib389.backend import Backends

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


def _set_dbsizes(inst, dbpagesize, dbcachesize):
    backends = Backends(inst)
    backend = backends.get(DEFAULT_BENAME)
    dir = backend.get_attr_val_utf8('nsslapd-directory')
    inst.stop()
    # Export the db to ldif
    ldif_file = f'{inst.get_ldif_dir()}/db.ldif'
    inst.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
               excludeSuffixes=None, repl_data=False,
               outputfile=ldif_file, encrypt=False)
    # modify dse.ldif
    dse_ldif = DSEldif(inst, serverid=inst.serverid)
    bdb = 'cn=bdb,cn=config,cn=ldbm database,cn=plugins,cn=config'
    dse_ldif.replace(bdb, 'nsslapd-db-page-size', str(dbpagesize))
    dse_ldif.replace(bdb, 'nsslapd-cache-autosize', '0')
    dse_ldif.replace(bdb, 'nsslapd-cache-autosize-split', '0')
    dse_ldif.replace(bdb, 'nsslapd-dbcachesize', str(dbcachesize))
    # remove the database files and the database environment files
    for d in (dir, inst.ds_paths.db_home_dir):
        for f in glob.glob(f'{d}/*'):
            if os.path.isfile(f):
                os.remove(f)
    # Reimport the db
    inst.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    inst.start()


@pytest.mark.ds50545
@pytest.mark.bz1795943
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
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



@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_dbmon_mp_pagesize(topology_st):
    """Test dbmon tool, that was ported from legacy tools to dsconf

    :id: 20c1e5b0-75a0-11ed-91de-482ae39447e5
    :setup: Standalone instance
    :steps:
         1. Set bdb parameters (pagesize and cachesize)
         2. Query ldbm database statistics and extract dbpages and dbcachesize values
         3. Capture dsconf monitor dbmon output and extract dbcache free_percentage value
         4. Check that free_percentage is computed rightly
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. free_percentage computation should be based on file system prefered block size
            rather than on db page size.

    """

    inst = topology_st.standalone
    fspath = inst.ds_paths.db_home_dir
    os.makedirs(fspath, mode=0o750, exist_ok=True)
    fs_pagesize = os.statvfs(fspath).f_bsize
    db_pagesize = 1024*64 # Maximum value supported by bdb
    if fs_pagesize == db_pagesize:
        fs_pagesize = db_pagesize / 2;
    _set_dbsizes(inst, db_pagesize, 80960)

    # Now lets check that we are really in the condition
    # needed to reproduce RHBZ 2034407
    ldbm_mon = MonitorLDBM(inst).get_status()
    dbcachesize = int(ldbm_mon['nsslapd-db-cache-size-bytes'][0])
    dbpages = int(ldbm_mon['nsslapd-db-pages-in-use'][0])

    args = FakeArgs()
    args.backends = DEFAULT_BENAME
    args.indexes = False
    args.json = True
    lc = LogCapture()
    db_monitor(inst, DEFAULT_SUFFIX, lc.log, args)
    db_mon_as_str = "".join( ( str(rec) for rec in lc.outputs ) )
    db_mon_as_str = re.sub("^[^{]*{", "{", db_mon_as_str)[:-2]
    db_mon = json.loads(db_mon_as_str);

    dbmon_free_percentage = int(10 * float(db_mon['dbcache']['free_percentage']))
    real_free_percentage = int(1000 * ( dbcachesize - dbpages * fs_pagesize ) / dbcachesize)
    log.info(f'dbcachesize: {dbcachesize}')
    log.info(f'dbpages: {dbpages}')
    log.info(f'db_pagesize: {db_pagesize}')
    log.info(f'fs_pagesize: {fs_pagesize}')
    log.info(f'dbmon_free_percentage: {dbmon_free_percentage}')
    log.info(f'real_free_percentage: {real_free_percentage}')
    assert real_free_percentage == dbmon_free_percentage


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

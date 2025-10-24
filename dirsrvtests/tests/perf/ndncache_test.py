#!/usr/bin/python3
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import re
import csv
import sys
import argparse, argcomplete
from abc import ABC, abstractmethod
from ldap.controls.sss import SSSRequestControl
from lib389.backend import Backends
from lib389.config import BDB_LDBMConfig, LMDB_LDBMConfig
from lib389.config import Config
from lib389.index import Indexes
from lib389._mapped_object import DSLdapObject
from lib389.monitor import MonitorBackend
from lib389.properties import TASK_WAIT
from lib389.topologies import topology_st as topo
from shutil import copyfile
from statistics import fmean, stdev


from lib389._constants import (
    DEFAULT_BENAME,
    DEFAULT_SUFFIX,
    DN_DM,
    DN_USERROOT_LDBM,
    PASSWORD,
)

from lib389.utils import (
    ldap, os, time, logging,
    ds_is_older,
    ensure_bytes,
    ensure_str,
    get_default_db_lib,
    get_ldapurl_from_serverid,
)

pytestmark = pytest.mark.tier3

THIS_DIR = os.path.dirname(__file__)
LDIF = os.path.join(THIS_DIR, '../data/5Kusers.ldif')
RESULT_DIR = f'{THIS_DIR}/../data/ndncache_test_results/r'
RESULT_FILE = f'{RESULT_DIR}/results_ndncache.'
CSV_FILE = f'{RESULT_DIR}/r.csv.'
NB_MEASURES = 100
NB_MEANINGFULL_MEASURES = int(NB_MEASURES/5)
SCENARIO='SCENARIO'
STAT_ATTRS2 =  ( 'currentdncachesize', 'currentdncachecount' )
STAT_ATTRS = ( 'dncachehits', 'dncachetries' ) + STAT_ATTRS2

WITHOUT_CACHE = 'without_ndn_cache'
WITH_CACHE = 'with_ndn_cache'


logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)
args = None

with open('/tmp/ndncache_test.log', 'w'):
    pass

def dbg(msg):
    pass
    # with open('/tmp/ndncache_test.log', 'a') as dbgfd:
    #     dbgfd.write(msg)


class Scenario:
    def __init__(self):
        self.ldc = None
        self.results = {}
        self._desc = None
        self._name = None

    def preop(self):
        # Run before measured operation
        pass

    @abstractmethod
    def op(self):
        # Operation to measure ( Should better be a single ldap operation )
        pass

    def postop(self):
        # Run after measured operation
        pass

    def __str__(self):
        return self._name

    def description(self):
        return self._desc

    @staticmethod
    def average(data):
        data.sort()
        log.info(f'RAW DATA: len(data)={len(data)} {data}')
        assert len(data) >= NB_MEANINGFULL_MEASURES
        for i in range(len(data)-1, NB_MEANINGFULL_MEASURES-1, -1):
            del data[i]
        log.info(f'COOKED DATA: len(data)={len(data)} {data}')
        m = fmean(data)
        v = stdev(data, m)
        v = v/m * 100
        return [ m, v, str(data) ]

    def getndncache_stats(self):
        dnbemon = 'cn=monitor,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'

        """The following fails ...
        mon = MonitorBackend(inst, dn=dnbemon)
        res = mon.get_status()
        log.debug(f'getndncache_stats res={res}')
        return [ res[a] for a in STAT_ATTRS ]
        """
        res = self.ldc.search_ext_s(
            base=dnbemon,
            scope=ldap.SCOPE_BASE,
            filterstr='(objectclass=*)',
            attrlist=list(STAT_ATTRS))
        log.debug(f'getndncache_stats res={res}')
        res = res[0][1]
        return { a: ensure_str(res[a][0]) for a in STAT_ATTRS }


    def measure(self, inst, conn):
        name = str(self)
        log.info(f'Perform {NB_MEASURES} of {name}')
        data = []
        self.ldc = conn
        aggregated_stats = { a: 0 for a in STAT_ATTRS }
        pattern = re.compile(br'.*optime=(\d+\.?\d*).*')
        dbg(f'Running measure on {self}\n')

        with  open(f'{inst.ds_paths.log_dir}/access', 'rb+') as logfd:
            try:
                for _ in range(NB_MEASURES):
                    self.preop()
                    pre_stats = self.getndncache_stats()
                    pos = os.fstat(logfd.fileno()).st_size
                    logfd.seek(pos)
                    dbg('Perform the operation\n')
                    self.op()
                    dbg('Done Performing the operation\n')
                    for line in iter(logfd.readline, b''):
                        res = pattern.match(line)
                        if res:
                            data.append(float(res.group(1)))
                    dbg('Done parsing log file\n')
                    post_stats = self.getndncache_stats()
                    for k,v in post_stats.items():
                        aggregated_stats[k] += float(v) - float(pre_stats[k])
                    self.postop()
                dbg('Compute average measure\n')
                result = Scenario.average(data)
            except ldap.LDAPError as exc:
                log.error(f'Scenario {name} failed because of {exc}')
                result = [ 0, 0, [] ]
        log.info(f'result (average, normalized standard deviation, data) of {name} is {result}')
        res_stats = [ str(aggregated_stats[a]) for a in STAT_ATTRS ]
        res_stats2 = [ post_stats[a] for a in STAT_ATTRS2 ]
        result.append(res_stats + res_stats2)
        return result


class Scen1(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen1'
        self._desc = 'Subtree search'

    def op(self):
        basedn = f'ou=Technology,ou=people,{DEFAULT_SUFFIX}'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(uid=*)',
            attrlist=['dn']
        )


class Scen2(Scenario):
    def __init__(self):
        super().__init__()
        self.sss_control = SSSRequestControl(criticality=True, ordering_rules=['modifiersName'])
        self._name = 'scen2'
        self._desc = 'Server Side Sorted Subtree search'

    def op(self):
        basedn = f'ou=Technology,ou=people,{DEFAULT_SUFFIX}'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(uid=*)',
            serverctrls=[self.sss_control],
            attrlist=['dn']
        )


class Scen3(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen3'
        self._desc = 'Subtree search with filter equality on dn syntax attribute'

    def op(self):
        basedn = f'ou=people,{DEFAULT_SUFFIX}'
        filter = 'modifiersName=uid=bmcdonald,ou=Information Technology,ou=people,dc=example,dc=com'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_SUBTREE,
            filterstr=filter,
            attrlist=['dn']
        )


class Scen4(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen4'
        self._desc = 'Modify member of large group with no substring index'

    def op(self):
        basedn = f'cn=all_users,ou=groups,{DEFAULT_SUFFIX}'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_DELETE, 'member', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)

    def postop(self):
        basedn = f'cn=all_users,ou=groups,{DEFAULT_SUFFIX}'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_ADD, 'member', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)


class Scen5(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen5'
        self._desc = 'Modify member of small group with no substring index'

    def op(self):
        basedn = f'cn=user_admin,ou=permissions,dc=example,dc=com'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_DELETE, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)

    def postop(self):
        basedn = f'cn=user_admin,ou=permissions,dc=example,dc=com'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_ADD, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)


class Scen6(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen6'
        self._desc = 'Search non indexed member in 1000 small groups'

    def op(self):
        basedn = f'ou=tinygroups,ou=groups,dc=example,dc=com'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(member=uid=Xgclements,ou=Quality Assurance,ou=people,dc=example,dc=com)',
            attrlist=['dn']
        )


class Scen7(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen7'
        self._desc = 'Search large group with small entrycache'

    def op(self):
        basedn = 'cn=all_users,ou=groups,dc=example,dc=com'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_BASE,
            filterstr='(objectclass=*)',
            attrlist=['dn']
        )

    def preop(self):
        basedn = f'ou=Technology,ou=people,{DEFAULT_SUFFIX}'
        result = self.ldc.search_ext_s(
            base=basedn,
            scope=ldap.SCOPE_SUBTREE,
            filterstr='(uid=*)',
            attrlist=['dn']
        )


SCENARIOS = [ Scen1(), Scen3(), Scen4(), Scen5(), Scen6(), ]
SCENARIOS_SMALL_ENTRYCACHE = [ Scen7(), ]
SCENARIOS_SKIPPED = [ Scen2(), ]


@pytest.fixture(scope="module")
def with_indexes(topo):
    # Add missing indexes (reindex is done by with_ldif fixture)
    inst = topo.standalone
    backends = Backends(inst)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()

    index = indexes.get('uid')
    index.ensure_attr_state( { 'nsIndexType': ['eq', 'pres'] } )

    index = indexes.create(properties={
        'cn': 'modifiersName',
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq']
        })

    index = indexes.get('member')
    index.delete()

    # index = indexes.get('uniquemember')
    # index.ensure_attr_state( { 'nsIndexType': ['eq', 'sub'] } )


@pytest.fixture(scope="module", params=[WITHOUT_CACHE,WITH_CACHE])
def with_ldif(topo, with_indexes, request):
    # Import ldif
    log.info('Run importLDIF task to add entries to Server')
    inst = topo.standalone
    # Import the ldif file
    try:
        inst.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=LDIF, args={TASK_WAIT: True})
        log.info('Online import succeded')
    except ValueError as e:
        log.error('Online import failed' + e.message('desc'))
        assert False
    # Enable/disable the ndn cache
    ndncache = b'off' if request.param == WITHOUT_CACHE else 'on'
    config = Config(inst)
    config.set('nsslapd-ndn-cache-enabled', ndncache)
    config.set('nsslapd-accesslog-logbuffering', 'off')
    return request.param


def set_ldap_attribute(conn, dn, attr, val):
    if val is None:
        vals = val
    else:
        vals = [ ensure_bytes(str(val)), ]
        conn.modify_s(dn, [(ldap.MOD_REPLACE, attr, vals),])


def open_conn(inst):
    ldapurl, certdir = get_ldapurl_from_serverid(inst.serverid)
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    conn.sasl_interactive_bind_s("", ldap.sasl.external())
    return conn


@pytest.fixture(scope="function")
def with_small_entrycache(topo, request):
    inst = topo.standalone
    if get_default_db_lib() == 'bdb':
        xdb_config_ldbm = BDB_LDBMConfig(inst)
    else:
        xdb_config_ldbm = LMDB_LDBMConfig(inst)
    config_ldbm = DSLdapObject(inst, DN_USERROOT_LDBM)
    conn = open_conn(inst)
    new_values = {
        'nsslapd-dbcachesize': 10,
        'nsslapd-cachememsize': 512000,  # Minimum value
        'nsslapd-dncachememsize': 100000000,
    }
    old_values = { attr: config_ldbm.get_attr_val_utf8(attr) for attr in new_values.keys() }
    old_autotune = config_ldbm.get_attr_val_utf8('nsslapd-cache-autosize')

    def fin():
        for k,v in old_values.items():
            set_ldap_attribute(conn, config_ldbm.dn, k, v)
        set_ldap_attribute(conn, config_ldbm.dn, 'nsslapd-cache-autosize', old_autotune)
        set_ldap_attribute(conn, xdb_config_ldbm.dn, 'nsslapd-cache-autosize', old_autotune)
        conn.unbind_s()

    request.addfinalizer(fin)
    set_ldap_attribute(conn, xdb_config_ldbm.dn, 'nsslapd-cache-autosize', 0)
    set_ldap_attribute(conn, config_ldbm.dn, 'nsslapd-cache-autosize', 0)
    for k,v in new_values.items():
        set_ldap_attribute(conn, config_ldbm.dn, k, v)
    return conn


def test_run_measure(topo, with_ldif):
    """Perform the measure on all scenarios and record the result

    :id: 0581e348-9d4f-11f0-a8cb-c85309d5c3e3
    :setup: Standalone instance
    :steps: 1. measure average optime for each sceanrios
    :expectedresults: no exception should occur
    """

    inst = topo.standalone
    conn = open_conn(inst)
    for scen in SCENARIOS:
        v = scen.measure(inst, conn)
        log.info(f'Set results ({with_ldif},{scen}): {v}')
        scen.results[with_ldif] = v
    conn.unbind_s()


def test_run_measure_with_small_entrycache(topo, with_ldif, with_small_entrycache):
    """Perform the measure on scenarios with small entrycache and record the result

    :id: 4290cc9c-a081-11f0-bc19-c85309d5c3e3
    :setup: Standalone instance
    :steps: 1. measure average optime for each sceanrios
    :expectedresults: no exception should occur
    """

    inst = topo.standalone
    conn = with_small_entrycache
    for scen in SCENARIOS_SMALL_ENTRYCACHE:
        v = scen.measure(inst, conn)
        log.info(f'Set results ({with_ldif},{scen}): {v}')
        scen.results[with_ldif] = v
    # conn.unbind_s is done by with_small_entrycache teardown


def test_log_results():
    """Perform the measure on all scenarios and record the result

    :id: b8131fee-9d50-11f0-a761-c85309d5c3e3
    :setup: None
    :steps: 1. display the results
    :expectedresults: no exception should occur
    """

    os.makedirs(RESULT_DIR, 0o755, exist_ok=True)
    statkeys = []
    for attr in STAT_ATTRS:
        statkeys.append(f'delta {attr} WITHOUT CACHE')
        statkeys.append(f'delta {attr} WITH CACHE')
    for attr in STAT_ATTRS2:
        statkeys.append(f'{attr} WITHOUT CACHE')
        statkeys.append(f'{attr} WITH CACHE')
    statkeys_str = "\t".join(statkeys)
    fname = numbered_filename(RESULT_FILE)
    with open(fname, 'w') as fout:
        fmt1='SCENARIO\tVALUE WITHOUT CACHE\tVALUE WITH CACHE\tGAIN\tDEVIATION WITHOUT CACHE\tDEVIATION WITH CACHE\tTEST DESCRIPTION'
        fout.write(f'{fmt1}\t{statkeys_str}\n')
        # fout.write(f'{fmt1}\tDATA WITHOUT CACHE\tDATA WITH CACHE\t{statkeys_str}\n')
        for scen in SCENARIOS + SCENARIOS_SMALL_ENTRYCACHE:
            log.debug(f'scen.results[WITHOUT_CACHE]={scen.results[WITHOUT_CACHE]}')
            log.debug(f'scen.results[WITH_CACHE]={scen.results[WITH_CACHE]}')
            m1, v1, d1, s1 = scen.results[WITHOUT_CACHE]
            m2, v2, d2, s2 = scen.results[WITH_CACHE]
            log.debug(f's1={s1}')
            log.debug(f's2={s2}')
            gain = ( m1 - m2 ) / m1 * 100 if m1 > 0 else '###'
            statvalues = []
            for idx in range(len(s1)):
                statvalues.append(s1[idx])
                statvalues.append(s2[idx])
            statvalues_str = "\t".join(statvalues)
            fmt1 = f'{scen}\t{m1:.5f}\t{m2:.5f}\t{gain:.1f}%\t{v1:.2f}%\t{v2:.2f}%\t{scen.description()}'
            fout.write(f'{fmt1}\t{statvalues_str}\n')
            # fout.write({fmt1}\t{d1}\t{d2}\t{statvalues_str}\n')


def numbered_filename(prefix):
    # Get a non excisting filename by adding a number to prefix
    idx = 1
    fname = f'{prefix}{idx}'
    while os.path.exists(fname):
        idx += 1
        fname = f'{prefix}{idx}'
    return fname


def move_results(dirname):
    if os.path.isdir(dirname):
        newdirname = numbered_filename(dirname)
        os.rename(dirname, newdirname)
        print(f'Result moved in {newdirname}')


def generate_csv(csvfilename):
    def parse_file(fname):
        res = []
        with open(fname, 'r') as fd:
            for line in fd:
                data = line.strip().split('\t')
                res.append(data)
        return res

    def update_gain(data, gain):
        k = data[0]
        if k != SCENARIO:
            if k not in gain:
                gain[k] = []
            gain[k].append(float(data[3][:-1]))


    # Parse result files and generate list of dict
    res1 = []
    gains = {}
    for idx in range(1, NB_MEASURES+1):
        fname = f'{RESULT_FILE}{idx}'
        if os.path.isfile(fname):
            res = parse_file(fname)
            res1.append(res)
            for data in res:
                update_gain(data, gains)

    if len(res1) < 2:
        print(f'ERROR: not enough result files {RESULT_FILE}*')
        sys.exit(1)

    scens = [ data[0] for data in res1[0] ]
    scens[0] = ''

    nbfiles = len(res1)
    nbscen = len(gains)
    nbdata = len(res1[1][1])

    nbrows = nbfiles * nbscen + 1
    nbcols = nbdata + 2 * nbscen + 4

    idx_graph_data = nbdata + 2
    idx_average = idx_graph_data + nbscen + 1

    print(f'nbfiles={nbfiles}')
    print(f'nbscen={nbscen}')
    print(f'nbdata={nbdata}')
    print(f'nbrows={nbrows}')
    print(f'nbcols={nbcols}')
    print(f'idx_graph_data={idx_graph_data}')
    print(f'idx_average={idx_average}')

    gtable = [ [ ' ' ] * nbcols  for _ in range(nbrows) ]
    # First Row
    for idx in range(nbdata):
        gtable[0][idx] = res1[0][0][idx]

    for idx,scen in enumerate(scens):
        if idx > 0:
            gtable[0][idx_graph_data+idx] = f'{scen} gain'
            gtable[0][idx_average+idx] = f'average {scen} gain'

    # Fill raw data table
    for idx, row in enumerate(res1):
        for idx2 in range(1, nbscen+1):
            data = row[idx2]
            for idx3, v in enumerate(data):
                gtable[1+nbfiles*(idx2-1)+idx][idx3] = v

    # Fill graph table
    for idx in range(1, nbfiles+1):
        for idx2 in range(1, nbscen+1):
            scen = scens[idx2]
            gtable[idx][idx_graph_data+idx2] = gains[scen][idx-1]
        gtable[idx][idx_graph_data] = idx

    # Fill average table
    for idx in range(1, nbscen+1):
        scen = scens[idx]
        gtable[1][idx_average+idx] = fmean(gains[scen])
    if args:
        gtable[3][idx_average] = str(args)

    # Write csv file
    with open(csvfilename, 'w', newline='') as csvfile:
        csvwriter = csv.writer(csvfile, delimiter='\t', quotechar='|', quoting=csv.QUOTE_MINIMAL)
        for row in gtable:
            csvwriter.writerow(row)


parser = argparse.ArgumentParser(allow_abbrev=True)
parser.add_argument('-b', '--batch',
        action='store_true',
        help="Run test in batch mode",
    )
parser.add_argument('-m', '--measure',
        type=int,
        help="Number of measured operations in a measure",
        default=NB_MEASURES
    )
parser.add_argument('-l', '--nbloops',
        type=int,
        help="Number of loops in batch mode",
        default=100
    )
parser.add_argument('-c', '--csv',
        action='store_true',
        help="generate csv file"
    )
argcomplete.autocomplete(parser)

if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    args = parser.parse_args()
    assert args.nbloops > 0
    assert args.measure >= 10
    NB_MEASURES = args.measure
    NB_MEANINGFULL_MEASURES = int(NB_MEASURES/5)
    if args.csv:
        # Generate csv file
        generate_csv(CSV_FILE)
        copyfile(CSV_FILE, '/tmp/r.csv') # Copy in /tmp to ease the import in google sheet
        sys.exit()
    if args.batch:
        # Run a series of nbloops tests
        move_results(RESULT_DIR)
        for idx in range(1, args.nbloops+1):
            print(f'\n#############\nRun #{idx}')
            pytest.main([CURRENT_FILE,])
        csvname = f'{RESULT_DIR}/r.csv'
        generate_csv(CSV_FILE)
        copyfile(CSV_FILE, '/tmp/r.csv') # Copy in /tmp to ease the import in google sheet
        move_results(RESULT_DIR)
        sys.exit()
    pytest.main([CURRENT_FILE,])
    sys.exit()

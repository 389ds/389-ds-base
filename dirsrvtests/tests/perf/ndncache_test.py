# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import re
from abc import ABC, abstractmethod
from ldap.controls.sss import SSSRequestControl
from lib389._constants import DN_DM, DEFAULT_SUFFIX, PASSWORD, DEFAULT_BENAME
from lib389.backend import Backends
from lib389.config import Config
from lib389.index import Indexes
from lib389.properties import TASK_WAIT
from lib389.topologies import topology_st as topo
from lib389.utils import ldap, os, time, logging, ds_is_older, get_ldapurl_from_serverid
from statistics import fmean, stdev

pytestmark = pytest.mark.tier3

THIS_DIR = os.path.dirname(__file__)
LDIF = os.path.join(THIS_DIR, '../data/5Kusers.ldif')
RESULT_FILE = f'{THIS_DIR}/results_ndncache'
NB_MEASURES = 100
NB_MEANINGFULL_MEASURES = NB_MEASURES-80

WITHOUT_CACHE = 'without_ndn_cache'
WITH_CACHE = 'with_ndn_cache'


logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)

class Scenario:
    def __init__(self):
        self.ldc = None
        self.results = {}
        self._desc = None
        self._name = None

    @abstractmethod
    def run(self):
        pass

    def __str__(self):
        return self._name

    def description(self):
        return self._desc


class Scen1(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen1'
        self._desc = 'Subtree search'

    def run(self):
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

    def run(self):
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

    def run(self):
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
        self._desc = 'Modify member of large group with substring index'

    def run(self):
        basedn = f'cn=all_users,ou=groups,{DEFAULT_SUFFIX}'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_DELETE, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)
        mods = [ ( ldap.MOD_ADD, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)


class Scen5(Scenario):
    def __init__(self):
        super().__init__()
        self._name = 'scen5'
        self._desc = 'Modify member of small group with substring index'

    def run(self):
        basedn = f'cn=user_admin,ou=permissions,dc=example,dc=com'
        value = b'uid=pwynn,ou=Information Technology,ou=people,dc=example,dc=com'
        mods = [ ( ldap.MOD_DELETE, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)
        mods = [ ( ldap.MOD_ADD, 'uniqueMember', [value,] ), ]
        result = self.ldc.modify_s(basedn, mods)


SCENARIOS = [ Scen1(), Scen3(), Scen4(), Scen5(), ]


@pytest.fixture(scope="module")
def with_indexes(topo):
    inst = topo.standalone
    # Add missing indexes
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


@pytest.fixture(scope="module", params=[WITHOUT_CACHE,WITH_CACHE])
def with_ldif(topo, with_indexes, request):
    """Import ldif"""

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
    return request.param


def average(data):
    data.sort()
    log.info(f'RAW DATA: {data}')
    for i in range(NB_MEASURES-1, NB_MEANINGFULL_MEASURES-1, -1):
        del data[i]
    log.info(f'COOKED DATA: {data}')
    m = fmean(data)
    v = stdev(data, m)
    v = v/m * 100
    return ( m, v, str(data) )


def measure(inst, func):
    name = str(func)
    log.info(f'Perform {NB_MEASURES} of {name}')
    data = []
    with  open(f'{inst.ds_paths.log_dir}/access', 'a+') as logfd:
        pos = os.fstat(logfd.fileno()).st_size
        for _ in range(NB_MEASURES):
            func.run()
        logfd.seek(pos)
        for line in iter(logfd.readline, ''):
            res = re.match(r'.*optime=(\d+\.?\d*).*', line)
            if res:
                data.append(float(res.group(1)))
    result = average(data)
    log.info(f'Average result of {name} is {result}')
    return result


def test_run_measure(topo, with_ldif):
    """Perform the measure on all scenarios and record the result

    :id: 0581e348-9d4f-11f0-a8cb-c85309d5c3e3
    :setup: Standalone instance
    :steps: 1. measure average optime for each sceanrios
    :expectedresults: no exception should occur
    """

    inst = topo.standalone
    ldapurl, certdir = get_ldapurl_from_serverid(inst.serverid)
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    conn.sasl_interactive_bind_s("", ldap.sasl.external())
    sss_control = SSSRequestControl(criticality=True, ordering_rules=['modifiersName'])

    for scen in SCENARIOS:
        scen.ldc = conn
        try:
            v = measure(inst, scen)
        except ldap.LDAPError as exc:
            log.error(f'Scenario {scen} failed because of {exc}')
            v = ( 0, 0 )
        log.info(f'Set results ({with_ldif},{scen}): {v}')
        scen.results[with_ldif] = v


def test_log_results():
    """Perform the measure on all scenarios and record the result

    :id: b8131fee-9d50-11f0-a761-c85309d5c3e3
    :setup: None
    :steps: 1. display the results
    :expectedresults: no exception should occur
    """

    idx = 1
    fname = f'{RESULT_FILE}.{idx}'
    while os.path.isfile(fname):
        idx += 1
        fname = f'{RESULT_FILE}.{idx}'
    with open(fname, 'w') as fout:
        fout.write(f'SCENARIO\tVALUE WITHOUT CACHE\tVALUE WITH CACHE\tGAIN\tDEVIATION WITHOUT CACHE\tDEVIATION WITH CACHE\tTEST DESCRIPTION\n')
        # fout.write(f'SCENARIO\tVALUE WITHOUT CACHE\tVALUE WITH CACHE\tGAIN\tDEVIATION WITHOUT CACHE\tVDEVIATION WITH CACHE\tTEST DESCRIPTION\tDATA WITHOUT CACHE\tDATA WITH CACHE\n')
        for scen in SCENARIOS:
            m1, v1, d1 = scen.results[WITHOUT_CACHE]
            m2, v2, d2 = scen.results[WITH_CACHE]
            gain = ( m1 - m2 ) / m1 * 100 if m1 > 0 else '###'
            # fout.write(f'{scen}\t{m1:.5f}\t{m2:.5f}\t{gain:.1f}%\t{v1:.2f}%\t{v2:.2f}%\t{scen.description()}\t{d1}\t{d2}\n')
            fout.write(f'{scen}\t{m1:.5f}\t{m2:.5f}\t{gain:.1f}%\t{v1:.2f}%\t{v2:.2f}%\t{scen.description()}\n')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))

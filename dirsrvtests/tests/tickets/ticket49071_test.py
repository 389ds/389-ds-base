import pytest
from lib389.utils import *
from lib389.topologies import topology_st as topo

from lib389._constants import DEFAULT_SUFFIX, TASK_WAIT

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_ticket49071(topo):
    """Verify- Import ldif with duplicate DNs, should not log error "unable to flush"

    :id: dce2b898-119d-42b8-a236-1130f58bff17
    :feature: It is to verify bug:1406101, ticket:49071
    :setup: Standalone instance, ldif file with duplicate entries
    :steps: 1. Create a ldif file with duplicate entries
            2. Import ldif file to DS
            3. Check error log file, it should not log "unable to flush"
            4. Check error log file, it should log "Duplicated DN detected"
    :expectedresults: Error log should not contain "unable to flush" error
    """

    log.info('ticket 49071: Create import file')
    l = """dn: dc=example,dc=com
objectclass: top
objectclass: domain
dc: example

dn: ou=myDups00001,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: myDups00001

dn: ou=myDups00001,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: myDups00001
"""

    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'data.ldif')
    with open(ldif_file, "w") as fd:
        fd.write(l)
        fd.close()

    log.info('ticket 49071: Import ldif having duplicate entry')
    try:
        topo.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                         input_file=ldif_file,
                                         args={TASK_WAIT: True})
    except ValueError:
        log.fatal('ticket 49104: Online import failed')
        raise

    log.info('ticket 49071: Error log should not have - unable to flush')
    assert not topo.standalone.ds_error_log.match('.*unable to flush.*')

    log.info('ticket 49071: Error log should have - Duplicated DN detected')
    assert topo.standalone.ds_error_log.match('.*Duplicated DN detected.*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


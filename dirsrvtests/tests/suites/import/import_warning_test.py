# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

from lib389.utils import *
from lib389.topologies import topology_st
from lib389.cli_conf.backend import *
from lib389.cli_base import FakeArgs

pytestmark = pytest.mark.tier1


DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def create_example_ldif(topology_st):
    ldif_dir = topology_st.standalone.get_ldif_dir()
    line1 = """version: 1

# entry-id: 1
dn: dc=example,dc=com
nsUniqueId: e5c4172a-97aa11eb-aaa8e47e-b1e12808
objectClass: top
objectClass: domain
dc: example
description: dc=example,dc=com
creatorsName: cn=Directory Manager
modifiersName: cn=Directory Manager
createTimestamp: 20210407140942Z
modifyTimestamp: 20210407140942Z
aci: (targetattr="dc || description || objectClass")(targetfilter="(objectClas
 s=domain)")(version 3.0; acl "Enable anyone domain read"; allow (read, search
 , compare)(userdn="ldap:///anyone");)

# entry-id: 3
dn: uid=demo,ou=People,dc=example,dc=com
objectClass: person
objectClass: inetOrgPerson
objectClass: organizationalPerson
objectClass: posixAccount
objectClass: top
uidNumber: 1119
gidNumber: 1000
nsUniqueId: 9a0e6603-a1cb11eb-aa2daeeb-95660ab0
creatorsName:
modifiersName: cn=directory manager
createTimestamp: 20210420112927Z
modifyTimestamp: 20210420113016Z
passwordGraceUserTime: 0
cn: demo
homeDirectory: /home/demo
uid: demo
sn: demo

"""
    with open(f'{ldif_dir}/warning_parent.ldif', 'w') as out:
        out.write(f'{line1}')
        os.chmod(out.name, 0o777)
    out.close()
    import_ldif1 = ldif_dir + '/warning_parent.ldif'
    return import_ldif1


@pytest.mark.skipif(ds_is_older('1.4.3.26'), reason="Fail because of bug 1951537")
@pytest.mark.bz1951537
@pytest.mark.ds4734
def test_import_warning(topology_st):
    """Import ldif file with skipped entries to generate a warning message

    :id: 66f9275b-11b4-4718-b401-18fa6011b362
    :setup: Standalone Instance
    :steps:
        1. Create LDIF file with skipped entries
        2. Import the LDIF file with backend import
        3. Check the topology logs
        4. Check errors log
    :expectedresults:
        1. Success
        2. Success
        3. Result message should contain warning code
        4. Errors log should contain skipped entry message
    """

    standalone = topology_st.standalone
    message = 'The import task has finished successfully, with warning code 8, check the logs for more detail'

    args = FakeArgs()
    args.be_name = 'userRoot'
    args.ldifs = [create_example_ldif(topology_st)]
    args.chunks_size = None
    args.encrypted = False
    args.gen_uniq_id = None
    args.only_core = False
    args.include_suffixes = 'dc=example,dc=com'
    args.exclude_suffixes = None

    log.info('Import the LDIF file')
    backend_import(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)

    log.info('Check logs for a warning message')
    assert topology_st.logcap.contains(message)
    assert standalone.ds_error_log.match('.*Skipping entry "uid=demo,ou=People,dc=example,dc=com" which has no parent.*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

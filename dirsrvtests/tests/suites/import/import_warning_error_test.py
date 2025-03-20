# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
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
from lib389._constants import DEFAULT_SUFFIX

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
dn: dc=example,dc=com
nsUniqueId: e5c4172a-97aa11eb-aaa8e47e-b1e12808
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

"""
    with open(f'{ldif_dir}/warning_parent.ldif', 'w') as out:
        out.write(f'{line1}')
        os.chmod(out.name, 0o777)
    out.close()
    import_ldif1 = ldif_dir + '/warning_parent.ldif'
    return import_ldif1

def create_example_bad_dn_ldif(topology_st):
    ldif_dir = topology_st.standalone.get_ldif_dir()
    line1 = """version: 1
# entry-id: 4
dn: demo3 People example com
objectClass: person
objectClass: inetOrgPerson
objectClass: organizationalPerson
objectClass: posixAccount
objectClass: top
uidNumber: 1112
gidNumber: 1000
nsUniqueId: 9a0e6603-a1cb11eb-aa2daeeb-95660ab6
creatorsName:
modifiersName: cn=directory manager
createTimestamp: 20210420112927Z
modifyTimestamp: 20210420113016Z
passwordGraceUserTime: 0
cn: demo3
homeDirectory: /home/demo
uid: demo3
sn: demo3

"""

    with open(f'{ldif_dir}/error_parent.ldif', 'w') as out:
        out.write(f'{line1}')
        os.chmod(out.name, 0o777)
    out.close()
    import_ldif2 = ldif_dir + '/error_parent.ldif'
    return import_ldif2

def create_clean_example_ldif(topology_st):
    ldif_dir = topology_st.standalone.get_ldif_dir()
    line1 = """version: 1
    
# entry-id: 5
# Parent entry for dc=demo4
dn: ou=People,dc=example,dc=com
objectClass: organizationalUnit
ou: People

# Parent entry for ou=People
dn: dc=example,dc=com
objectClass: domain
dc: example

dn: uid=demo4,ou=People,dc=example,dc=com
objectClass: person
objectClass: inetOrgPerson
objectClass: organizationalPerson
objectClass: posixAccount
objectClass: top
uidNumber: 1112
gidNumber: 1000
nsUniqueId: 9a0e6603-a1cb13eb-aa2daeeb-95660ab6
creatorsName:
modifiersName: cn=directory manager
createTimestamp: 20210420112927Z
modifyTimestamp: 20210420113016Z
passwordGraceUserTime: 0
cn: demo4
homeDirectory: /home/demo
uid: demo4
sn: demo4

"""

    with open(f'{ldif_dir}/clean_parent.ldif', 'w') as out:
        out.write(f'{line1}')
        os.chmod(out.name, 0o777)
    out.close()
    import_ldif2 = ldif_dir + '/clean_parent.ldif'
    return import_ldif2


def test_import_warning(topology_st):
    """Import ldif file with duplicate DNs Unique ID entries to generate a warning message

    :id: 80b0d0c8-8498-11ee-b0fa-e40d365eed59
    :setup: Standalone Instance
    :steps:
        1. Create LDIF file with Duplicate DN entries
        2. Import the LDIF file with backend import
        3. Check the topology logs
        4. Check errors log for Duplicate DN entry message
        5. Check errors log for Duplicate Unique ID entry message
    :expectedresults:
        1. Success
        2. Success
        3. Result message should contain warning code
        4. Errors log should contain Duplicate DN entry message
        5. Errors log should contain Duplicate Unique ID entry message
    """

    standalone = topology_st.standalone
    
    args = FakeArgs()
    args.be_name = 'userRoot'
    args.ldifs = [create_example_ldif(topology_st)]
    args.chunks_size = None
    args.encrypted = False
    args.gen_uniq_id = None
    args.only_core = False
    args.include_suffixes = 'dc=example,dc=com'
    args.exclude_suffixes = None
    args.timeout = 0
    dn_test = "dc=example,dc=com"
    uid_test = "e5c4172a-97aa11eb-aaa8e47e-b1e12808"
    exp_msg_dup_dn = f"Entry has multiple dns \"{dn_test}\" and \"{dn_test}\" (second ignored)\n"
    exp_msg_dup_uid = f"Entry has multiple uniqueids {uid_test} and {uid_test} (second ignored)\n"
    log.info('Import the LDIF file')
    backend_import(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)

    log.info('Check logs for a warning message')
    str2_dup_err_warn_log_lst = ([warn_log for warn_log in (standalone.ds_error_log.readlines()) if "str2entry_dupcheck" in warn_log])
    stripped_err_warn_lst = set ([s[69:] for s in str2_dup_err_warn_log_lst])
    _lst = [exp_msg_dup_dn,exp_msg_dup_uid]
    for msg in _lst:
        log.info(f"Checking for {msg} warning in Logs")
        assert msg in stripped_err_warn_lst,(f"Test Failed - Unable to find {msg}.")
        log.info(f"Found warning {msg}") 

def test_invalid_DN_ignored_skipped(topology_st):
    """Import ldif file with Invalid DN entries to generate a Error message

    :id: 80b0d0c8-8498-11ee-b0fa-e40d365eed79
    :setup: Standalone Instance
    :steps:
        1. Create LDIF file with Invalid DN
        2. Import the LDIF file with backend import
        3. Check the topology logs

    :expectedresults:
        1. Success
        2. Success
        3. Result message should contain warning code
        4. Errors log should contain Invalid DN Error message

    """
    
    standalone = topology_st.standalone
    
    args = FakeArgs()
    args.be_name = 'userRoot'
    args.ldifs = [create_example_bad_dn_ldif(topology_st)]
    args.chunks_size = None
    args.encrypted = False
    args.gen_uniq_id = None
    args.only_core = False
    args.include_suffixes = 'dc=example,dc=com'
    args.exclude_suffixes = None
    args.timeout = 0
    
    backend_import(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    str_skip_invalid_dn = "Skipping entry \"demo3 People example com\" which has an invalid DN"
    log.info('Check logs for a Skipping entry message')
    str2_dup_err_log_lst = [skip_log for skip_log in (standalone.ds_error_log.readlines()) if "invalid DN" in skip_log ]
    assert any(str_skip_invalid_dn in log for log in str2_dup_err_log_lst)

def test_clean_ldif_no_errors_warnings(topology_st):
    """Import a clean ldif file with no Invalid DN or duplicate DN entries

    :id: 80b0d0c8-8498-11ee-b0fa-e40d465eed79
    :setup: Standalone Instance
    :steps:
        1. Create LDIF file with clean LDIF no erorrs
        2. Import the LDIF file with backend import
        3. Check the topology logs

    :expectedresults:
        1. Success
        2. Success
        3. Result message should contain no warning or errors
        4. Errors log should contain no warnings or errors

    """
    
    standalone = topology_st.standalone
    args = FakeArgs()
    args.be_name = 'userRoot'
    args.ldifs = [create_clean_example_ldif(topology_st)]
    args.chunks_size = None
    args.encrypted = False
    args.gen_uniq_id = None
    args.only_core = False
    args.include_suffixes = 'dc=example,dc=com'
    args.exclude_suffixes = None
    args.timeout = 0
    
    backend_import(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    
    log.info('Check logs for a Warnings and Error messages')
    str2_dup_err_err_log_lst = [err_log for err_log in (standalone.ds_error_log.readlines()) if "ERR - str2entry_dupcheck" in err_log ]
    str2_dup_warn_log_lst = [warn_log for warn_log in (standalone.ds_error_log.readlines()) if "WARN - str2entry_dupcheck" in warn_log ]
    assert "ERR - str2entry_dupcheck" not in str2_dup_err_err_log_lst
    assert "WARN - str2entry_dupcheck" not in str2_dup_warn_log_lst

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

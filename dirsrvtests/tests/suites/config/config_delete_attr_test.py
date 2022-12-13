# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest

from lib389.utils import os, logging, ds_is_older, ldap
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]


@pytest.mark.ds48961
def test_delete_storagescheme(topology_st):
    """ Test that deletion of passwordStorageScheme is rejected

    :id: 53ab2dbf-e37c-4d30-8cce-0d5f44ed204a
    :setup: Standalone instance
    :steps:
         1. Create instance
         2. Modify passwordStorageScheme attribute
         3. Remove passwordStorageScheme attribute
         4. Check exception message
    :expectedresults:
         1. Success
         2. Success
         3. Removal should be rejected
         4. Message should be about rejected change
    """

    standalone = topology_st.standalone

    log.info('Check we can modify passwordStorageScheme')
    standalone.config.set('passwordStorageScheme', 'CLEAR')
    assert standalone.config.get_attr_val_utf8('passwordStorageScheme') == 'CLEAR'

    log.info('Check removal of passwordStorageScheme is rejected')
    with pytest.raises(ldap.OPERATIONS_ERROR) as excinfo:
        standalone.config.remove('passwordStorageScheme', None)
        assert "deleting the value is not allowed" in str(excinfo.value)


@pytest.mark.ds48961
def test_reset_attributes(topology_st):
    """ Test that we can reset some attributes while others are rejected

    :id: 5f78088f-36d3-4a0b-8c1b-4abc161e996f
    :setup: Standalone instance
    :steps:
         1. Create instance
         2. Check attributes from attr_to_test can be reset
         3. Check value of that attribute is empty
         4. Check reset of attributes from attr_to_fail is rejected
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone

    # These attributes should not be able to reset
    attr_to_fail = [
        'nsslapd-localuser',
        'nsslapd-defaultnamingcontext',
        'nsslapd-accesslog',
        'nsslapd-auditlog',
        'nsslapd-securitylog',
        'nsslapd-errorlog',
        'nsslapd-tmpdir',
        'nsslapd-rundir',
        'nsslapd-bakdir',
        'nsslapd-certdir',
        'nsslapd-instancedir',
        'nsslapd-ldifdir',
        'nsslapd-lockdir',
        'nsslapd-schemadir',
        'nsslapd-workingdir',
        'nsslapd-localhost',
        'nsslapd-certmap-basedn',
        'nsslapd-port',
        'nsslapd-secureport',
        'nsslapd-rootpw',
        'nsslapd-hash-filters',
        'nsslapd-requiresrestart',
        'nsslapd-plugin',
        'nsslapd-privatenamespaces',
        'nsslapd-allowed-to-delete-attrs',
        'nsslapd-accesslog-list',
        'nsslapd-auditfaillog-list',
        'nsslapd-auditlog-list',
        'nsslapd-errorlog-list',
        'nsslapd-config',
        'nsslapd-versionstring',
        'objectclass',
        'cn',
        'nsslapd-backendconfig',
        'nsslapd-betype',
        'nsslapd-connection-buffer',
        'nsslapd-malloc-mmap-threshold',
        'nsslapd-malloc-mxfast',
        'nsslapd-malloc-trim-threshold',
        'nsslapd-referralmode',
        'nsslapd-saslpath',
        'passwordadmindn'
    ]

    attr_to_test = {
        'nsslapd-listenhost': 'localhost',
        'nsslapd-securelistenhost': 'localhost',
        'nsslapd-allowed-sasl-mechanisms': 'GSSAPI',
        'nsslapd-svrtab': 'Some data'
    }

    for attr in attr_to_test:
        newval = attr_to_test[attr]

        log.info("Change %s value to --> %s" % (attr, newval))
        standalone.config.set(attr, newval)
        assert standalone.config.get_attr_val_utf8(attr) == newval

        log.info('Now reset the attribute')
        standalone.config.reset(attr)
        assert standalone.config.get_attr_val_utf8(attr) == ''
        log.info("%s is reset to None" % attr)

    for attr in attr_to_fail:
        log.info("Resetting %s" % attr)
        try:
            standalone.config.reset(attr)
            # Shouldn't reach here, the reset should fail!
            log.info('Attribute deletion should fail => test failed!')
            assert False
        except (ldap.UNWILLING_TO_PERFORM, ldap.OPERATIONS_ERROR, ldap.OBJECT_CLASS_VIOLATION):
            log.info('Change was rejected, test passed')
            pass
        except ldap.NO_SUCH_ATTRIBUTE:
            log.info("This attribute isn't part of cn=config, so is already default!")
            pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

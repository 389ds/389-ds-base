# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import pytest
from lib389.tasks import *
from test389.topologies import topology_st as topo
from lib389.utils import *
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389.dseldif import DSEldif
from lib389.encrypted_attributes import EncryptedAttrs
from lib389.cli_ctl.dblib import DbscanHelper

pytestmark = pytest.mark.tier1

USER_DN = 'uid=test_user,%s' % DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def enable_user_attr_encryption(topo, request):
    """ Enables attribute encryption for various attributes
        Adds a test user with encrypted attributes
    """

    log.info("Enable TLS for attribute encryption")
    topo.standalone.enable_tls()

    log.info("Enables attribute encryption")
    backends = Backends(topo.standalone)
    backend = backends.list()[0]
    encrypt_attrs = EncryptedAttrs(topo.standalone, basedn='cn=encrypted attributes,{}'.format(backend.dn))
    log.info("Enables attribute encryption for employeeNumber and telephoneNumber")
    emp_num_encrypt = encrypt_attrs.create(properties={'cn': 'employeeNumber', 'nsEncryptionAlgorithm': 'AES'})
    telephone_encrypt = encrypt_attrs.create(properties={'cn': 'telephoneNumber', 'nsEncryptionAlgorithm': '3DES'})

    log.info("Add a test user with encrypted attributes")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('employeeNumber', '1000')
    test_user.replace('telephoneNumber', '1234567890')

    def fin():
        log.info("Remove attribute encryption for various attributes")
        emp_num_encrypt.delete()
        telephone_encrypt.delete()

    request.addfinalizer(fin)
    return test_user


def test_basic(topo, enable_user_attr_encryption):
    """Tests encrypted attributes with a test user entry

    :id: d767d5c8-b934-4b14-9774-bd13480d81b3
    :setup: Standalone instance
            Enable AES encryption config on employeenumber
            Enable 3DES encryption config on telephoneNumber
            Add a test user with with encrypted attributes
    :steps:
         1. Restart the server
         2. Check employeenumber encryption enabled
         3. Check telephoneNumber encryption enabled
         4. Check that encrypted attribute is present for user i.e. telephoneNumber
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
    """

    log.info("Restart the server")
    topo.standalone.restart()
    backends = Backends(topo.standalone)
    backend = backends.list()[0]
    encrypt_attrs = backend.get_encrypted_attrs()

    log.info("Extracting values of cn from the list of objects in encrypt_attrs")
    log.info("And appending the cn values in a list")
    enc_attrs_cns = []
    for enc_attr in encrypt_attrs:
        enc_attrs_cns.append(enc_attr.rdn)

    log.info("Check employeenumber encryption is enabled")
    assert "employeeNumber" in enc_attrs_cns

    log.info("Check telephoneNumber encryption is enabled")
    assert "telephoneNumber" in enc_attrs_cns

    log.info("Check that encrypted attribute is present for user i.e. telephoneNumber")
    assert enable_user_attr_encryption.present('telephoneNumber')


def test_export_import_ciphertext(topo, enable_user_attr_encryption):
    """Configure attribute encryption, store some data, check that we can export the ciphertext

    :id: b433e215-2926-48a5-818f-c21abc40fc2d
    :setup: Standalone instance
            Enable AES encryption config on employeenumber
            Enable 3DES encryption config on telephoneNumber
            Add a test user with encrypted attributes
    :steps:
         1. Export data as ciphertext
         2. Check that the attribute is present in the exported file
         3. Check that the encrypted value of attribute is not present in the exported file
         4. Delete the test user entry with encrypted data
         5. Import the previously exported data as ciphertext
         6. Check attribute telephoneNumber should be imported
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
    """

    log.info("Export data as ciphertext")
    export_ldif = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_ciphertext.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=(DEFAULT_SUFFIX,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_ldif):
        log.fatal('Failed to run offline db2ldif')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is present in the exported file")
    log.info("Check that the encrypted value of attribute is not present in the exported file")
    with open(export_ldif, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'telephoneNumber' in ldif
        assert 'telephoneNumber: 1234567890' not in ldif

    log.info("Delete the test user entry with encrypted data")
    enable_user_attr_encryption.delete()

    log.info("Import data as ciphertext, which was exported previously")
    import_ldif = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_ciphertext.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=(DEFAULT_SUFFIX,),
                                   excludeSuffixes=None, encrypt=False, import_file=import_ldif):
        log.fatal('Failed to run offline ldif2db')
        assert False
    topo.standalone.start()

    log.info("Check that the data with encrypted attribute is imported properly")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('testuser')
    assert user.present("telephoneNumber")


def test_export_import_plaintext(topo, enable_user_attr_encryption):
    """Configure attribute encryption, store some data, check that we can export the plain text

     :id: b171e215-0456-48a5-245f-c21abc40fc2d
     :setup: Standalone instance
             Enable AES encryption config on employeenumber
             Enable 3DES encryption config on telephoneNumber
             Add a test user with encrypted attributes
     :steps:
          1. Export data as plain text
          2. Check that the attribute is present in the exported file
          3. Check that the encrypted value of attribute is also present in the exported file
          4. Delete the test user entry with encrypted data
          5. Import data as plaintext
          6. Check attribute value of telephoneNumber
     :expectedresults:
          1. This should be successful
          2. This should be successful
          3. This should be successful
          4. This should be successful
          5. This should be successful
          6. This should be successful
     """

    log.info("Export data as plain text")
    export_ldif = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_plaintext.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=(DEFAULT_SUFFIX,),
                                   excludeSuffixes=None, encrypt=True, repl_data=None, outputfile=export_ldif):
        log.fatal('Failed to run offline db2ldif')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is present in the exported file")
    log.info("Check that the plain text value of the encrypted attribute is present in the exported file")
    with open(export_ldif, 'r') as ldif_file:
        assert 'telephoneNumber: 1234567890' in ldif_file.read()

    log.info("Delete the test user entry with encrypted data")
    enable_user_attr_encryption.delete()

    log.info("Import data as plain text, which was exported previously")
    import_ldif = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_plaintext.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=(DEFAULT_SUFFIX,),
                                   excludeSuffixes=None, encrypt=True, import_file=import_ldif):
        log.fatal('Failed to run offline ldif2db')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is imported properly")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.get('testuser')
    assert user.present("telephoneNumber")


def test_attr_encryption_unindexed(topo, enable_user_attr_encryption):
    """Configure attribute encryption for an un-indexed attribute, check that we can export encrypted data

    :id: d3ef38e1-bb5a-44d8-a3a4-4a25a57e3454
    :setup: Standalone instance
            Enable AES encryption config on employeenumber
            Enable 3DES encryption config on telephoneNumber
            Add a test user with encrypted attributes
    :steps:
         1. Export data as cipher text
         2. Check that the unindexed attribute employeenumber is present in exported ldif file
         3. Check that the unindexed attribute employeenumber value is not present in exported ldif file
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
    """
    log.info("Export data as cipher text")
    export_ldif = os.path.join(topo.standalone.ds_paths.ldif_dir, "emp_num_ciphertext.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=(DEFAULT_SUFFIX,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_ldif):
        log.fatal('Failed to run offline db2ldif')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is present in the exported file")
    log.info("Check that the encrypted value of attribute is not present in the exported file")
    with open(export_ldif, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'employeeNumber' in ldif
        assert 'employeeNumber: 1000' not in ldif


def test_attr_encryption_multiple_backends(topo, enable_user_attr_encryption):
    """Tests Configuration of attribute encryption for multiple backends
       Where both the backends have attribute encryption

    :id: 9ece3e6c-96b7-4dd5-b092-d76dda23472d
    :setup: Standalone instance
            SSL Enabled
    :steps:
         1. Add two test backends
         2. Configure attribute encryption for telephoneNumber in one test backend
         3. Configure attribute encryption for employeenumber in another test backend
         4. Add a test user in both backends with encrypted attributes
         5. Export data as ciphertext from both backends
         6. Check that telephoneNumber is encrypted in the ldif file of db1
         7. Check that employeeNumber is encrypted in the ldif file of db2
         8. Delete both test backends
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
         7. This should be successful
         8. This should be successful
    """
    log.info("Add two test backends")
    test_suffix1 = 'dc=test1,dc=com'
    test_db1 = 'test_db1'
    test_suffix2 = 'dc=test2,dc=com'
    test_db2 = 'test_db2'

    # Create backends
    backends = Backends(topo.standalone)
    backend = backends.list()[0]
    test_backend1 = backends.create(properties={'cn': test_db1,
                                                'nsslapd-suffix': test_suffix1})
    test_backend2 = backends.create(properties={'cn': test_db2,
                                                'nsslapd-suffix': test_suffix2})

    # Create the top of the tree
    suffix1 = Domain(topo.standalone, test_suffix1)
    test1 = suffix1.create(properties={'dc': 'test1'})
    suffix2 = Domain(topo.standalone, test_suffix2)
    test2 = suffix2.create(properties={'dc': 'test2'})

    log.info("Enables attribute encryption for telephoneNumber in test_backend1")
    backend1_encrypt_attrs = EncryptedAttrs(topo.standalone, basedn='cn=encrypted attributes,{}'.format(test_backend1.dn))
    b1_encrypt = backend1_encrypt_attrs.create(properties={'cn': 'telephoneNumber',
                                                           'nsEncryptionAlgorithm': 'AES'})

    log.info("Enables attribute encryption for employeeNumber in test_backend2")
    backend2_encrypt_attrs = EncryptedAttrs(topo.standalone, basedn='cn=encrypted attributes,{}'.format(test_backend2.dn))
    b2_encrypt = backend2_encrypt_attrs.create(properties={'cn': 'employeeNumber',
                                                           'nsEncryptionAlgorithm': 'AES'})

    log.info("Add a test user with encrypted attributes in both backends")
    users = UserAccounts(topo.standalone, test1.dn, None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('telephoneNumber', '1234567890')

    users = UserAccounts(topo.standalone, test2.dn, None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('employeeNumber', '1000')

    log.info("Export data as ciphertext from both backends")
    export_db1 = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_db1.ldif")
    export_db2 = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_db2.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.db2ldif(bename=test_db1, suffixes=(test_suffix1,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_db1):
        log.fatal('Failed to run offline db2ldif')
        assert False

    if not topo.standalone.db2ldif(bename=test_db2, suffixes=(test_suffix2,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_db2):
        log.fatal('Failed to run offline db2ldif')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is present in the exported file in db1")
    log.info("Check that the encrypted value of attribute is not present in the exported file in db1")
    with open(export_db1, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'telephoneNumber' in ldif
        assert 'telephoneNumber: 1234567890' not in ldif

    log.info("Check that the attribute is present in the exported file in db2")
    log.info("Check that the encrypted value of attribute is not present in the exported file in db2")
    with open(export_db2, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'employeeNumber' in ldif
        assert 'employeeNumber: 1000' not in ldif

    log.info("Delete test backends")
    test_backend1.delete()
    test_backend2.delete()


def test_attr_encryption_backends(topo, enable_user_attr_encryption):
    """Tests Configuration of attribute encryption for single backend
       where more backends are present

    :id: f3ef40e1-17d6-44d8-a3a4-4a25a57e9064
    :setup: Standalone instance
            SSL Enabled
    :steps:
         1. Add two test backends
         2. Configure attribute encryption for telephoneNumber in one test backend
         3. Add a test user in both backends with telephoneNumber
         4. Export ldif from both test backends
         5. Check that telephoneNumber is encrypted in the ldif file of db1
         6. Check that telephoneNumber is not encrypted in the ldif file of db2
         7. Delete both test backends
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
         7. This should be successful
    """
    log.info("Add two test backends")
    test_suffix1 = 'dc=test1,dc=com'
    test_db1 = 'test_db1'
    test_suffix2 = 'dc=test2,dc=com'
    test_db2 = 'test_db2'

    # Create backends
    backends = Backends(topo.standalone)
    test_backend1 = backends.create(properties={'cn': test_db1,
                                                'nsslapd-suffix': test_suffix1})
    test_backend2 = backends.create(properties={'cn': test_db2,
                                                'nsslapd-suffix': test_suffix2})

    # Create the top of the tree
    suffix1 = Domain(topo.standalone, test_suffix1)
    test1 = suffix1.create(properties={'dc': 'test1'})
    suffix2 = Domain(topo.standalone, test_suffix2)
    test2 = suffix2.create(properties={'dc': 'test2'})

    log.info("Enables attribute encryption for telephoneNumber in test_backend1")
    backend1_encrypt_attrs = EncryptedAttrs(topo.standalone, basedn='cn=encrypted attributes,{}'.format(test_backend1.dn))
    b1_encrypt = backend1_encrypt_attrs.create(properties={'cn': 'telephoneNumber',
                                                           'nsEncryptionAlgorithm': 'AES'})

    log.info("Add a test user with telephoneNumber in both backends")
    users = UserAccounts(topo.standalone, test1.dn, None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('telephoneNumber', '1234567890')

    users = UserAccounts(topo.standalone, test2.dn, None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('telephoneNumber', '1234567890')

    log.info("Export data as ciphertext from both backends")
    export_db1 = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_db1.ldif")
    export_db2 = os.path.join(topo.standalone.ds_paths.ldif_dir, "export_db2.ldif")

    # Offline export
    topo.standalone.stop()
    if not topo.standalone.db2ldif(bename=test_db1, suffixes=(test_suffix1,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_db1):
        log.fatal('Failed to run offline db2ldif')
        assert False

    if not topo.standalone.db2ldif(bename=test_db2, suffixes=(test_suffix2,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_db2):
        log.fatal('Failed to run offline db2ldif')
        assert False
    topo.standalone.start()

    log.info("Check that the attribute is present in the exported file in db1")
    log.info("Check that the encrypted value of attribute is not present in the exported file in db1")
    with open(export_db1, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'telephoneNumber' in ldif
        assert 'telephoneNumber: 1234567890' not in ldif

    log.info("Check that the attribute is present in the exported file in db2")
    log.info("Check that the value of attribute is also present in the exported file in db2")
    with open(export_db2, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'telephoneNumber' in ldif
        assert 'telephoneNumber: 1234567890' in ldif

    log.info("Delete test backends")
    test_backend1.delete()
    test_backend2.delete()


def test_attr_encryption_compare_values(topo, enable_user_attr_encryption):
    """Tests that different attributes have different encrypted values
       even if the clear values are the same.

    :id: f91d9d10-89a5-11f1-8476-c85309d5c3e3
    :setup: Standalone instance
            SSL Enabled
    :steps:
         1. Add a test backends
         2. Configure attribute encryption for telephoneNumber
         3. Configure attribute encryption for employeenumber
         4. Add a test user with encrypted attributes having same values
         5. Export data as ciphertext and as cleartext
         6. Check that telephoneNumber and employeeNumber are present in the ciphertext test entry
         7. Check that telephoneNumber and employeeNumber values are encrypted
         8. Check that telephoneNumber and employeeNumber values are different
         9. Check that telephoneNumber and employeeNumber are present in the cleartext test entry
         10. Check that telephoneNumber and employeeNumber values are the expected one
         11. Check that telephoneNumber and employeeNumber index keys for the user test entry are different
         12. Delete test backend
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
         7. This should be successful
         8. This should be successful
         9. This should be successful
         10. This should be successful
         11. This should be successful
         12. This should be successful
    """
    inst = topo.standalone
    log.info("Add test backend")
    test_dc = 'test'
    test_suffix = f'dc={test_dc},dc=com'
    test_db = 'test_db'
    test_value = '1234'

    # Create backends
    backends = Backends(inst)
    test_backend = backends.create(properties={'cn': test_db,
                                                'nsslapd-suffix': test_suffix})

    # Create the top of the tree
    suffix1 = Domain(inst, test_suffix)
    test1 = suffix1.create(properties={'dc': test_dc})
    # Add employeeNumber index
    test_backend.add_index('employeeNumber', [ 'eq', ])
    # Backend contains only the suffix entry
    # So we can avoid reindexing but still need to restart to initialize the lmdb dbis
    # (Note: using reindex=True in add_index leads to a race condition )
    inst.restart()

    log.info("Enables attribute encryption for telephoneNumber in test_backend1")
    backend1_encrypt_attrs = EncryptedAttrs(inst, basedn='cn=encrypted attributes,{}'.format(test_backend.dn))
    backend1_encrypt_attrs.create(properties={'cn': 'telephoneNumber',
                                              'nsEncryptionAlgorithm': 'AES'})
    backend1_encrypt_attrs.create(properties={'cn': 'employeeNumber',
                                              'nsEncryptionAlgorithm': 'AES'})

    log.info("Add a test user with both encrypted attributes having same value")
    users = UserAccounts(inst, test1.dn, None)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.replace('telephoneNumber', test_value)
    test_user.replace('employeeNumber', test_value)

    log.info("Export data as ciphertext")
    export_db_ciphertext = os.path.join(inst.ds_paths.ldif_dir, "export_db_ciphertext.ldif")
    export_db_cleartext = os.path.join(inst.ds_paths.ldif_dir, "export_db_cleartext.ldif")

    # Offline export
    inst.stop()
    if not inst.db2ldif(bename=test_db, suffixes=(test_suffix,),
                                   excludeSuffixes=None, encrypt=False, repl_data=None, outputfile=export_db_ciphertext):
        log.fatal('Failed to run offline db2ldif (ciphertext)')
        assert False
    if not inst.db2ldif(bename=test_db, suffixes=(test_suffix,),
                                   excludeSuffixes=None, encrypt=True, repl_data=None, outputfile=export_db_cleartext):
        log.fatal('Failed to run offline db2ldif (cleartext)')
        assert False
    inst.start()

    ldif = DSEldif(inst, path=export_db_ciphertext)

    log.info("Check that the attributes are present in the ciphertext exported file")
    telephoneNumber = ldif.get(test_user.dn, 'telephoneNumber', single=True)
    employeeNumber = ldif.get(test_user.dn, 'employeeNumber', single=True)
    assert telephoneNumber is not None
    assert employeeNumber is not None
    log.info("Check that the encrypted value of attribute is not present in the exported file")
    assert telephoneNumber != test_value
    assert employeeNumber  != test_value
    log.info("Check that the encrypted values of attributes are not the same")
    assert telephoneNumber != employeeNumber

    ldif = DSEldif(inst, path=export_db_cleartext)
    log.info("Check that the attributes are present in the cleartext exported file")
    telephoneNumber = ldif.get(test_user.dn, 'telephoneNumber', single=True)
    employeeNumber = ldif.get(test_user.dn, 'employeeNumber', single=True)
    assert telephoneNumber is not None
    assert employeeNumber is not None
    log.info("Check that the decrypted value of attribute are the expected one")
    assert telephoneNumber == test_value
    assert employeeNumber  == test_value

    log.info('Check that telephoneNumber and employeeNumber index keys for the user test entry are different')
    dbsh = DbscanHelper(inst)
    db_telephoneNumber = dbsh.get_dbi('telephoneNumber', backend=test_db)
    db_employeeNumber = dbsh.get_dbi('employeeNumber', backend=test_db)
    # Dumps the indexes as dict
    data_telephoneNumber = dbsh.list_dbi_data(db_telephoneNumber)
    data_employeeNumber = dbsh.list_dbi_data(db_employeeNumber)
    id = test_user.get_attr_val_int('entryid')
    dbid = id.to_bytes( 4, 'little').hex()
    log.info(f'data_telephoneNumber: {data_telephoneNumber}')
    log.info(f'data_employeeNumber: {data_employeeNumber}')
    log.info(f'Test user entryid is {id} aka {dbid}')
    # Restrict to the equality keys matching the entryid
    keys_telephoneNumber = [ k for k,v in data_telephoneNumber.items() if dbid in v and k.startswith('3d')]
    keys_employeeNumber = [ k for k,v in data_employeeNumber.items() if dbid in v and k.startswith('3d')]
    log.info(f'keys_telephoneNumber: {keys_telephoneNumber}')
    log.info(f'keys_employeeNumber: {keys_employeeNumber}')
    # Should only have one value for the entry on each attribute
    assert len(keys_telephoneNumber) == 1
    assert len(keys_employeeNumber) == 1
    # And the keys should be different
    assert keys_telephoneNumber != keys_employeeNumber

    log.info("Delete test backend")
    test_backend.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

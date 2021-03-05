import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m1 as topo
from lib389.backend import Backends
from lib389.encrypted_attributes import EncryptedAttrs

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

SECOND_SUFFIX = 'o=namingcontext'
THIRD_SUFFIX = 'o=namingcontext2'

def test_be_delete(topo):
    """Test that we can delete a backend that contains replication
    configuration and encrypted attributes.  The default naming 
    context should also be updated to reflect the next available suffix

    :id: 5208f897-7c95-4925-bad0-9ceb95fee678
    :setup: Supplier Instance
    :steps:
        1. Create second backend/suffix
        2. Add an encrypted attribute to the default suffix
        3. Delete default suffix
        4. Check the nsslapd-defaultnamingcontext is updated
        5. Delete the last backend
        6. Check the namingcontext has not changed
        7. Add new backend
        8. Set default naming context
        9. Verify the naming context is correct
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """
    
    inst = topo.ms["supplier1"] 
    
    # Create second suffix      
    backends = Backends(inst)
    default_backend = backends.get(DEFAULT_SUFFIX)
    new_backend = backends.create(properties={'nsslapd-suffix': SECOND_SUFFIX,
                                              'name': 'namingRoot'})
  
    # Add encrypted attribute entry under default suffix
    encrypt_attrs = EncryptedAttrs(inst, basedn='cn=encrypted attributes,{}'.format(default_backend.dn))
    encrypt_attrs.create(properties={'cn': 'employeeNumber', 'nsEncryptionAlgorithm': 'AES'})
    
    # Delete default suffix
    default_backend.delete()
    
    # Check that the default naming context is set to the new/second suffix
    default_naming_ctx = inst.config.get_attr_val_utf8('nsslapd-defaultnamingcontext')
    assert default_naming_ctx == SECOND_SUFFIX

    # delete new backend, but the naming context should not change
    new_backend.delete()

    # Check that the default naming context is still set to the new/second suffix
    default_naming_ctx = inst.config.get_attr_val_utf8('nsslapd-defaultnamingcontext')
    assert default_naming_ctx == SECOND_SUFFIX

    # Add new backend
    new_backend = backends.create(properties={'nsslapd-suffix': THIRD_SUFFIX,
                                              'name': 'namingRoot2'})

    # manaully set naming context
    inst.config.set('nsslapd-defaultnamingcontext', THIRD_SUFFIX)

    # Verify naming context is correct
    default_naming_ctx = inst.config.get_attr_val_utf8('nsslapd-defaultnamingcontext')
    assert default_naming_ctx == THIRD_SUFFIX


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])


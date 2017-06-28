import os

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389._constants import (DEFAULT_SUFFIX, LOCALHOST, SER_HOST, SER_PORT,
                              SER_SERVERID_PROP, SER_CREATION_SUFFIX, SER_INST_SCRIPTS_ENABLED,
                              PORT_STANDALONE, args_instance)

from lib389 import DirSrv

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def create_instance(config_attr):
    log.info('create_instance - Installs the instance and Sets the value of InstScriptsEnabled to true OR false.')

    log.info("Set up the instance and set the config_attr")
    # Create instance
    standalone = DirSrv(verbose=False)

    # Args for the instance
    args_instance[SER_HOST] = LOCALHOST
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = 'standalone'
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_instance[SER_INST_SCRIPTS_ENABLED] = config_attr
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()
    return standalone


@pytest.mark.parametrize("config_attr", ('true', 'false'))
def test_slapd_InstScriptsEnabled(config_attr):
    """Try to set InstScriptsEnabled attribute
    to various config_attrs as default, true and false

    :ID: 02faac7f-c44d-4a3e-bf2d-1021e51da1ed
    :feature: Add configure option to disable instance specific scripts
    :setup: Create directory server instance using setup-ds.pl
            with slapd.InstScriptsEnabled option as "True" and "False"
    :steps: 1. Execute setup-ds.pl with slapd.InstScriptsEnabled option as "True" and "False" one by one
            2. Check if /usr/lib64/dirsrv/slapd-instance instance script directory is created or not.
            3. The script directory should be created if slapd.InstScriptsEnabled option is "True"
            4. The script directory should not be created if slapd.InstScriptsEnabled option is "False"
    :expectedresults: The script directory should be created
                      if slapd.InstScriptsEnabled option is "True" and not if it is "Fasle"
    """

    log.info('set SER_INST_SCRIPTS_ENABLED to {}'.format(config_attr))
    standalone = create_instance(config_attr)

    # Checking the presence of instance script directory when SER_INST_SCRIPTS_ENABLED is set to true and false
    if config_attr == 'true':
        log.info('checking the presence of instance script directory when SER_INST_SCRIPTS_ENABLED is set to true')
        assert os.listdir('/usr/lib64/dirsrv/slapd-standalone')

    elif config_attr == 'false':
        log.info('checking instance script directory does not present when SER_INST_SCRIPTS_ENABLED is set to false')
        assert not os.path.exists("/usr/lib64/dirsrv/slapd-standalone")

    # Remove instance
    standalone.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

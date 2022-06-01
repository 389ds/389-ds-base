import pytest
from lib389.utils import *
from lib389._constants import (DEFAULT_SUFFIX, SER_HOST, SER_PORT,
                               SER_SERVERID_PROP, SER_CREATION_SUFFIX, SER_INST_SCRIPTS_ENABLED,
                               args_instance, ReplicaRole)

from lib389 import DirSrv

pytestmark = pytest.mark.tier0

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def create_instance(config_attr):
    log.info('create_instance - Installs the instance and Sets the value of InstScriptsEnabled to true OR false.')

    log.info("Set up the instance and set the config_attr")
    instance_data = generate_ds_params(1, ReplicaRole.STANDALONE)
    # Create instance
    standalone = DirSrv(verbose=False)

    # Args for the instance
    args_instance[SER_HOST] = instance_data[SER_HOST]
    args_instance[SER_PORT] = instance_data[SER_PORT]
    args_instance[SER_SECURE_PORT] = instance_data[SER_SECURE_PORT]
    args_instance[SER_SERVERID_PROP] = instance_data[SER_SERVERID_PROP]
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_instance[SER_INST_SCRIPTS_ENABLED] = config_attr
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()
    return standalone

# During UI & CLI rebase in 1.4.3 (8abefc754351dac6163c669d3087b8721e6e796c)
# the use of setup-ds.pl was dropped
# only support 'false'
@pytest.mark.parametrize("config_attr", ('false'))
def test_slapd_InstScriptsEnabled(config_attr):
    """Tests InstScriptsEnabled attribute with "True" and "False" options

    :id: 02faac7f-c44d-4a3e-bf2d-1021e51da1ed
    :parametrized: yes
    :setup: Standalone instance with slapd.InstScriptsEnabled option as "True" and "False"

    :steps:
         1. Execute setup-ds.pl with slapd.InstScriptsEnabled option as "True".
         2. Check if /usr/lib64/dirsrv/slapd-instance instance script directory is created or not.
         3. Execute setup-ds.pl with slapd.InstScriptsEnabled option as "False".
         4. Check if /usr/lib64/dirsrv/slapd-instance instance script directory is created or not.

    :expectedresults:
         1. Instance should be created.
         2. /usr/lib64/dirsrv/slapd-instance instance script directory should be created.
         3. Instance should be created.
         4. /usr/lib64/dirsrv/slapd-instance instance script directory should not be created.
    """

    log.info('set SER_INST_SCRIPTS_ENABLED to {}'.format(config_attr))
    standalone = create_instance(config_attr)

    # Checking the presence of instance script directory when SER_INST_SCRIPTS_ENABLED is set to true and false
    if config_attr == 'true':
        log.info('checking the presence of instance script directory when SER_INST_SCRIPTS_ENABLED is set to true')
        assert os.listdir('/usr/lib64/dirsrv/slapd-standalone1')

    elif config_attr == 'false':
        log.info('checking instance script directory does not present when SER_INST_SCRIPTS_ENABLED is set to false')
        assert not os.path.exists("/usr/lib64/dirsrv/slapd-standalone1")

    # Remove instance
    standalone.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


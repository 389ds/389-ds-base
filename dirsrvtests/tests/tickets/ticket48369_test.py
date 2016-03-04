import os
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from ldap.controls.ppolicy import PasswordPolicyControl


logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48369(topology):
    """
    Test RFE 48369 - return password policy controls by default without needing
    to be requested.
    """

    DN = 'uid=test,' + DEFAULT_SUFFIX

    #
    # Setup password policy
    #
    try:
        topology.standalone.modify_s('cn=config', [(ldap.MOD_REPLACE,
                                                    'passwordExp',
                                                    'on'),
                                                   (ldap.MOD_REPLACE,
                                                    'passwordMaxAge',
                                                    '864000'),
                                                   (ldap.MOD_REPLACE,
                                                    'passwordSendExpiringTime',
                                                    'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set config: %s' % str(e))
        assert False

    #
    # Add entry
    #
    try:
        topology.standalone.add_s(Entry((DN,
            {'objectclass': 'top extensibleObject'.split(),
             'uid': 'test',
             'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user entry: %s' % str(e))
        assert False
    time.sleep(1)

    #
    # Bind as the new user, and request the control
    #
    try:
        msgid = topology.standalone.simple_bind(DN, "password",
            serverctrls=[PasswordPolicyControl()])
        res_type, res_data, res_msgid, res_ctrls = \
            topology.standalone.result3(msgid)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind: %s: Error %s' % (ctl_resp, str(e)))
        assert False

    if res_ctrls[0].controlType == PasswordPolicyControl.controlType:
        ppolicy_ctrl = res_ctrls[0]
    else:
        log.fatal('Control not found')
        assert False

    log.info('Time until expiration (%s)' %
             repr(ppolicy_ctrl.timeBeforeExpiration))

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
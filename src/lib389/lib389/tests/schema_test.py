# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest

from lib389 import DirSrv
from lib389._constants import SER_HOST, SER_PORT, SER_SERVERID_PROP, LOCALHOST
from lib389.schema import Schema

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'


class TopologyInstance(object):
    def __init__(self, instance):
        instance.open()
        self.instance = instance


@pytest.fixture(scope="module")
def topology(request):
    instance = DirSrv(verbose=False)
    instance.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)
    if instance.exists():
        instance.delete()
    instance.create()
    instance.open()

    def fin():
        instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)


def test_schema(topology):
    must_expect = ['uidObject', 'account', 'posixAccount', 'shadowAccount']
    may_expect = ['cosDefinition', 'inetOrgPerson', 'inetUser',
                  'mailRecipient']
    attrtype, must, may = topology.instance.schema.query_attributetype('uid')
    assert attrtype.names == ('uid', 'userid')
    for oc in must:
        assert oc.names[0] in must_expect
    for oc in may:
        assert oc.names[0] in may_expect
    assert topology.instance.schema.query_objectclass('account').names == \
        ('account', )

def test_schema_reload(topology):
    """Test that the appropriate task entry is created when reloading schema."""
    schema = Schema(topology.instance)
    task = schema.reload()
    assert task.exists()
    task.wait()
    assert task.get_exit_code() == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    import os
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

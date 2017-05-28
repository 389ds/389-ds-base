# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest

from lib389._constants import *
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


@pytest.mark.parametrize("entry_dn", (DN_CONFIG,
                                      DN_CONFIG_LDBM))
def test_get_singlevalue(topo, entry_dn):
    """Check that we can get an attribute value under different suffixes"""

    dse_ldif = DSEldif(topo.standalone)

    log.info("Get 'cn' attr from {}".format(entry_dn))
    attr_values = dse_ldif.get(entry_dn, "cn")
    assert attr_values == ["config"]

    log.info("Get 'nonexistent' attr from {}".format(entry_dn))
    attr_values = dse_ldif.get(entry_dn, "nonexistent")
    assert not attr_values


def test_get_multivalue(topo):
    """Check that we can get attribute values"""

    dse_ldif = DSEldif(topo.standalone)

    log.info("Get objectClass from {}".format(DN_CONFIG))
    attr_values = dse_ldif.get(DN_CONFIG, "objectClass")
    assert len(attr_values) == 3
    assert "top" in attr_values
    assert "extensibleObject" in attr_values
    assert "nsslapdConfig" in attr_values


@pytest.mark.parametrize("fake_attr_value", ("fake value",
                                             "fakevalue"))
def test_add(topo, fake_attr_value):
    """Check that we can add an attribute to a given suffix"""

    dse_ldif = DSEldif(topo.standalone)
    fake_attr = "fakeAttr"

    log.info("Add {} to {}".format(fake_attr, DN_CONFIG))
    dse_ldif.add(DN_CONFIG, fake_attr, fake_attr_value)
    attr_values = dse_ldif.get(DN_CONFIG, fake_attr)
    assert attr_values == [fake_attr_value]

    log.info("Clean up")
    dse_ldif.delete(DN_CONFIG, fake_attr)
    assert not dse_ldif.get(DN_CONFIG, fake_attr)


def test_replace(topo):
    """Check that we can replace an attribute to a given suffix"""

    dse_ldif = DSEldif(topo.standalone)
    port_attr = "nsslapd-port"
    port_value = "390"

    log.info("Get default value of {}".format(port_attr))
    default_value = dse_ldif.get(DN_CONFIG, port_attr)[0]

    log.info("Replace {} with {}".format(port_attr, port_value))
    dse_ldif.replace(DN_CONFIG, port_attr, port_value)
    attr_values = dse_ldif.get(DN_CONFIG, port_attr)
    assert attr_values == [port_value]

    log.info("Restore default value")
    dse_ldif.replace(DN_CONFIG, port_attr, default_value)


def test_delete_singlevalue(topo):
    """Check that we can delete an attribute from a given suffix"""

    dse_ldif = DSEldif(topo.standalone)
    fake_attr = "fakeAttr"
    fake_attr_values = ["fake1", "fake2", "fake3"]

    log.info("Add multivalued {} to {}".format(fake_attr, DN_CONFIG))
    for value in fake_attr_values:
        dse_ldif.add(DN_CONFIG, fake_attr, value)

    log.info("Delete {}".format(fake_attr_values[0]))
    dse_ldif.delete(DN_CONFIG, fake_attr, fake_attr_values[0])
    attr_values = dse_ldif.get(DN_CONFIG, fake_attr)
    assert len(attr_values) == 2
    assert fake_attr_values[0] not in attr_values
    assert fake_attr_values[1] in attr_values
    assert fake_attr_values[2] in attr_values

    log.info("Clean up")
    dse_ldif.delete(DN_CONFIG, fake_attr)
    assert not dse_ldif.get(DN_CONFIG, fake_attr)


def test_delete_multivalue(topo):
    """Check that we can delete attributes from a given suffix"""

    dse_ldif = DSEldif(topo.standalone)
    fake_attr = "fakeAttr"
    fake_attr_values = ["fake1", "fake2", "fake3"]

    log.info("Add multivalued {} to {}".format(fake_attr, DN_CONFIG))
    for value in fake_attr_values:
        dse_ldif.add(DN_CONFIG, fake_attr, value)

    log.info("Delete all values of {}".format(fake_attr))
    dse_ldif.delete(DN_CONFIG, fake_attr)
    assert not dse_ldif.get(DN_CONFIG, fake_attr)


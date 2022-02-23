# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import os
import logging
import pytest
import ldap

from lib389.topologies import topology_st as topology
from lib389._constants import DEFAULT_SUFFIX
from lib389.schema import Schema

pytestmark = pytest.mark.tier1

def test_x_descr_oid(topology):
    """Test import of an attribute using descr-oid format that starts
    with an X-. This should "fail" with a descriptive error message.

    :id: 9308bdbd-363c-45a9-8223-9a6c925dba37

    :setup: Standalone instance

    :steps:
        1. Add invalid x-attribute
        2. Add valid x-attribute
        3. Add invalid x-object
        4. Add valid x-object

    :expectedresults:
        1. raises INVALID_SYNTAX
        2. success
        3. raises INVALID_SYNTAX
        4. success
    """
    inst = topology.standalone

    schema = Schema(inst)

    with pytest.raises(ldap.INVALID_SYNTAX):
        schema.add('attributeTypes', "( x-attribute-oid NAME 'x-attribute' DESC 'desc' EQUALITY  caseIgnoreIA5Match SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 X-ORIGIN 'user defined' )")
    schema.add('attributeTypes', "( 1.2.3.4.5.6.7.8.9.10 NAME 'x-attribute' DESC 'desc' EQUALITY  caseIgnoreIA5Match SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 X-ORIGIN 'user defined' )")

    with pytest.raises(ldap.INVALID_SYNTAX):
        schema.add('objectClasses', "( x-object-oid NAME 'x-object' DESC 'desc' SUP TOP AUXILIARY MAY ( x-attribute ) X-ORIGIN 'user defined' )")
    schema.add('objectClasses', "( 1.2.3.4.5.6.7.8.9.11 NAME 'x-object' DESC 'desc' SUP TOP AUXILIARY MAY ( x-attribute ) X-ORIGIN 'user defined' )")


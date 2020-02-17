# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import sys
import io
from lib389.cli_base import LogCapture
from lib389._constants import *
from lib389.topologies import create_topology, DEBUGGING
from lib389.configurations import get_sample_entries


@pytest.fixture(scope="module")
def topology(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_be_latest(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'nsslapd-suffix': DEFAULT_SUFFIX,
    })
    # Now apply sample entries
    centries = get_sample_entries(INSTALL_LATEST_CONFIG)
    cent = centries(topology.standalone, DEFAULT_SUFFIX)
    cent.apply()

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


@pytest.fixture(scope="module")
def topology_be_001003006(request):
    topology = create_topology({ReplicaRole.STANDALONE: 1}, None)
    topology.standalone.backends.create(properties={
        'cn': 'userRoot',
        'nsslapd-suffix': DEFAULT_SUFFIX,
    })
    # Now apply sample entries
    centries = get_sample_entries('001003006')
    cent = centries(topology.standalone, DEFAULT_SUFFIX)
    cent.apply()

    def fin():
        if DEBUGGING:
            topology.standalone.stop()
        else:
            topology.standalone.delete()
    request.addfinalizer(fin)

    topology.logcap = LogCapture()
    return topology


def check_output(some_string, missing=False, ignorecase=True):
    """Check the output of captured STDOUT.  This assumes "sys.stdout = io.StringIO()"
    otherwise there would be nothing to read.  Flush IO after performing check.
    :param some_string - text, or list of strings, to search for in output
    :param missing - test if some_string is NOT present in output
    :param ignorecase - Set whether to ignore the character case in both the output
                        and some_string
    """
    output = sys.stdout.getvalue()
    is_list = isinstance(some_string, list)

    if ignorecase:
        output = output.lower()
        if is_list:
            some_string = [text.lower() for text in some_string]
        else:
            some_string = some_string.lower()

    if missing:
        if is_list:
            for text in some_string:
                assert(text not in output)
        else:
            assert(some_string not in output)
    else:
        if is_list:
            for text in some_string:
                assert(text in output)
        else:
            assert(some_string in output)

    # Clear the buffer
    sys.stdout = io.StringIO()

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.utils import ds_is_newer
from lib389._constants import INSTALL_LATEST_CONFIG
from .config_001003006 import c001003006, c001003006_sample_entries
from .config_001004000 import c001004000, c001004000_sample_entries
from .config_001004002 import c001004002, c001004002_sample_entries
from .config_002003000 import c002003000, c002003000_sample_entries


def get_config(version):
    # We do this to avoid test breaking on older version that may
    # not expect the new default layout.
    if (version == INSTALL_LATEST_CONFIG and ds_is_newer('2.3.0')):
        return c002003000
    if (version == INSTALL_LATEST_CONFIG and ds_is_newer('1.4.2')):
        return c001004002
    elif (version == INSTALL_LATEST_CONFIG and ds_is_newer('1.4.0')):
        return c001004000
    elif (version == INSTALL_LATEST_CONFIG):
        return c001003006
    elif (version == '002003000' and ds_is_newer('2.3.0')):
        return c001004002
    elif (version == '001004002' and ds_is_newer('1.4.2')):
        return c001004002
    elif (version == '001004000' and ds_is_newer('1.4.0')):
        return c001004000
    elif (version == '001003006'):
        return c001003006
    raise Exception('version %s no match' % version)


def get_sample_entries(version):
    if (version == INSTALL_LATEST_CONFIG):
        return c002003000_sample_entries
    elif (version == '002003000'):
        return c002003000_sample_entries
    elif (version == '001004002'):
        return c001004002_sample_entries
    elif (version == '001004000'):
        return c001004000_sample_entries
    elif (version == '001003006'):
        return c001003006_sample_entries
    raise Exception('version %s no match' % version)


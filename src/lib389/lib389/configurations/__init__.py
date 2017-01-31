# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._constants import INSTALL_LATEST_CONFIG
from .config_001003006 import c001003006, c001003006_sample_entries

def get_config(version):
    if (version == INSTALL_LATEST_CONFIG):
        return c001003006
    if (version == '001003006'):
        return c001003006
    raise Exception('version %s no match' % version)

def get_sample_entries(version):
    if (version == INSTALL_LATEST_CONFIG):
        return c001003006_sample_entries
    if (version == '001003006'):
        return c001003006_sample_entries
    raise Exception('version %s no match' % version)


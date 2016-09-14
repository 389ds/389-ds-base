# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from lib389.paths import Paths

# Test that we can retrieve the settings from the paths object
def test_paths():
    # Make the paths object.
    p = Paths()
    # Get a value!
    v = p.version

# Test that if we make the path object, and we don't read a path from it
# the filecache state is False
def test_path_noread():
    p = Paths()
    assert(p._defaults_cached is False)
    p._read_defaults()
    assert(p._defaults_cached is True)

def test_path_exception():
    # Trigger the internal path find with a "bad location" and
    # make sure that we get the exception
    p = Paths()
    try:
        p._get_defaults_loc(search_paths=[])
        assert(False)
    except IOError:
        assert(True)


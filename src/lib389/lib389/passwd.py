# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
This file contains helpers to generate password hashes compatible for
Directory Server.
"""

import subprocess
import random
import string
import os

# How do we feed our prefix into this?
def password_hash(pw, scheme=None, bin_dir='/bin'):
    # Check that the binary exists
    pwdhashbin = os.path.join(bin_dir, 'pwdhash')
    assert(os.path.isfile(pwdhashbin))
    if scheme is None:
        h = subprocess.check_output([pwdhashbin, pw]).strip()
    else:
        h = subprocess.check_output([pwdhashbin, '-s', scheme, pw]).strip()
    return h.decode('utf-8')


def password_generate(length=64):
    pw = [random.choice(string.ascii_letters) for x in range(length - 1)]
    pw.append('%s' % random.randint(0, 9))
    return "".join(pw)

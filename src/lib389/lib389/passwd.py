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
import sys

BESTSCHEME = 'SSHA512'
MAJOR, MINOR, _, _, _ = sys.version_info

# We need a dict of the schemes I think .... 
PWSCHEMES = [
    'SHA1',
    'SHA256',
    'SHA512',
    'SSHA',
    'SSHA256',
    'SSHA512',
]

# How do we feed our prefix into this?
def password_hash(pw, scheme=BESTSCHEME, prefix='/'):
    # Check that the binary exists
    assert(scheme in PWSCHEMES)
    pwdhashbin = os.path.join(prefix, 'bin', 'pwdhash-bin')
    assert(os.path.isfile(pwdhashbin))
    h = subprocess.check_output([pwdhashbin, '-s', scheme, pw]).strip()
    return h.decode('utf-8')

def password_generate(length=64):
    pw = None
    if MAJOR >= 3:
        pw =  [random.choice(string.ascii_letters) for x in range(length)]
    else:
        pw =  [random.choice(string.letters) for x in xrange(length)]
    return "".join(pw)

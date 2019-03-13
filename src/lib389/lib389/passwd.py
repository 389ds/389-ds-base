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
    """Generate a password hash using pwdhash tool

    :param pw: the password
    :type pw: str
    :param scheme: password scheme to be used
        (e.g. MD5, SHA1, SHA256, SHA512, SSHA, SSHA256, SSHA512)
    :type scheme: str
    :param bin_dir: a path to the directory with pwdhash tool
    :type bin_dir: str

    :returns: a string with a password hash
    """

    # Check that the binary exists
    pwdhashbin = os.path.join(bin_dir, 'pwdhash')
    assert(os.path.isfile(pwdhashbin))
    if scheme is None:
        h = subprocess.check_output([pwdhashbin, pw]).strip()
    else:
        h = subprocess.check_output([pwdhashbin, '-s', scheme, pw]).strip()
    return h.decode('utf-8')


def password_generate(length=64):
    """Generate a complex password with at least
    one upper case letter, a lower case letter, a digit
    and a special character. The special characters are limited
    to a set that can be highlighted with double-click to allow
    easier copy-paste to a password-manager. Most password strength
    comes from length anyway, so this is why we use a long length (64)

    :param length: a password length
    :type length: int

    :returns: a string with a password
    """

    # We have exactly 64 characters because it makes the selection unbiased
    # The number of possible values for a byte is 256 which is a multiple of 64
    # Maybe it is an overkill for our case but it can come handy one day
    # (especially consider the fact we can use it for CLI tools)
    #
    # So it turns out we don't escape the - properly, which means that in certain
    # cases the "chars" yield a string like "-ntoauhtnonhtunothu", which of course
    # means that the pwdhash binary says "no such option -n".
    chars = string.ascii_letters + string.digits + '.'

    # Get the minimal requirements
    # Don't use characters that prevent easy highlight for copy paste ...
    # It's the little details that make us great
    pw = [random.choice(string.ascii_lowercase),
          random.choice(string.ascii_uppercase),
          random.choice(string.digits),
          '.']

    # Use the simple algorithm to generate more or less secure password
    for i in range(length - 3):
        pw.append(chars[os.urandom(1)[0] % len(chars)])
    random.shuffle(pw)
    return "".join(pw)

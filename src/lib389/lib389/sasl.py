# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Lib389 python ldap sasl operations.

These should be upstreamed if possible.
"""

from ldap.sasl import sasl, CB_AUTHNAME, CB_PASS


class LdapSSOTokenSASL(sasl):
    """
    This class handles draft-wibrown-ldapssotoken-01 authentication.
    """

    def __init__(self, token):
        auth_dict = {CB_PASS: token}
        sasl.__init__(self, auth_dict, "LDAPSSOTOKEN")


class PlainSASL(sasl):
    """
    This class handles PLAIN sasl authentication
    """

    def __init__(self, authz_id, passwd):
        auth_dict = {CB_AUTHNAME: authz_id, CB_PASS: passwd}
        sasl.__init__(self, auth_dict, "PLAIN")


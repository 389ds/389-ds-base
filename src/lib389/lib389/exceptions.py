# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap


class Error(Exception):
    pass


class InvalidArgumentError(Error):
    pass


class AlreadyExists(ldap.ALREADY_EXISTS):
    pass


class NoSuchEntryError(ldap.NO_SUCH_OBJECT):
    pass


class MissingEntryError(NoSuchEntryError):
    """When just added entries are missing."""
    pass


class UnwillingToPerformError(Error):
    pass


class NotImplementedError(Error):
    pass


class DsError(Error):
    """Generic DS Error."""
    pass

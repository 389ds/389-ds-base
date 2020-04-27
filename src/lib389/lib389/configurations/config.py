# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# These are the operation runners for configuring a server to look like a certain
# version.

# Generally these are one way, upgrade only.

class baseconfig(object):
    def __init__(self, instance):
        self._instance = instance

    def apply_config(self, install=False, upgrade=False, interactive=False):
        # Go through the list of operations
        # Can we assert the types?
        for op in self._operations:
            op.apply(install, upgrade, interactive)

# This just serves as a base
class configoperation(object):
    def __init__(self, instance):
        self._instance = instance
        self.install = True
        self.upgrade = True
        self.description = None

    def apply(self, install, upgrade, interactive):
        # How do we want to handle interactivity?
        if not ((install and self.install) or (upgrade and self.upgrade)):
            self._instance.debug()
            return False
        if interactive:
            raise Exception('Interaction not yet supported')
        self._apply()

    def _apply(self):
        # The consumer must over-ride this.
        raise Exception('Not implemented!')


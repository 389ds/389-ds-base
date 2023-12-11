# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import os
import re
import copy
from lib389._mapped_object_lint import DSLint
from lib389 import pid_from_file
from lib389.lint import DSTHPLE0001

class Tunables(DSLint):
    """A class for working with system tunables
    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        self._instance = instance
        self.pid = str(pid_from_file(instance.ds_paths.pid_file))


    @classmethod
    def lint_uid(cls):
        return 'tunables'


    def _lint_thp(self):
        """Check if THP is enabled"""
        def systemwide_thp_enabled() -> bool:
            thp_path = '/sys/kernel/mm/transparent_hugepage'
            thp_enabled_path = os.path.join(thp_path, "enabled")
            thp_status_pattern = r"(.*\[always\].*)|(.*\[madvise\].*)"
            if os.path.exists(thp_enabled_path):
                with open(thp_enabled_path, 'r') as f:
                    thp_status = f.read().strip()
                    match = re.match(thp_status_pattern, thp_status)
                    return match is not None


        def instance_thp_enabled() -> bool:
            pid_status_path = f"/proc/{self.pid}/status"

            with open(pid_status_path, 'r') as pid_status:
                pid_status_content = pid_status.read()
                thp_line = None
                for line in pid_status_content.split('\n'):
                    if 'THP_enabled' in line:
                        thp_line = line
                        break
                if thp_line is not None:
                    thp_value = int(thp_line.split()[1])
                    return bool(thp_value)


        if instance_thp_enabled() and systemwide_thp_enabled():
            report = copy.deepcopy(DSTHPLE0001)
            report['check'] = 'tunables:transparent_huge_pages'
            yield report


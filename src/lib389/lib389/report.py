# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

""" Helper module to generate pytest report"""

import glob
import ldap
import sys
import os
import subprocess
from lib389 import DirSrv
from lib389.utils import Paths

p = Paths()

# Generic filter class that logs everything
class LogFilter:
    def __init__(self):
        self.stop_now = False
        self.last_line = None

    def filter(self, line):
        self.last_line = line
        return True

# An error log filter class that only keep emergency, errors and critical errors
class ErrorLogFilter(LogFilter):
    def filter(self, line):
        self.last_line = line
        if "MDB_MAP_FULL" in line:
            self.stop_now = True
        for keyword in ( "CRIT", "EMERG", "ERR" ):
            if f"- {keyword} -" in line:
                return True
        return False


# Concat filtered lines from a file as a string
# filter instance must have filter.filter(line) that returns a boolean
# and a filter.stop_now boolean
def log2Report(path, filter=LogFilter()):
    res=""
    with open(path) as file:
        for line in file:
            if filter.filter(line) is True:
                res += line
            if filter.stop_now is True:
                break
    return res

# Log a list of items
def loglist(list):
    text = ""
    for item in list:
        text += "	f{item}\n"
    return text

# Log cores file
def logcorefiles():
    cmd = [ "/usr/bin/coredumpctl", "info", "ns-slapd" ]
    coreinfo = subprocess.run(cmd, capture_output=True, shell=False, check=False, text=True)
    text = ""
    text += coreinfo.stdout
    text += "\n\ncoredumpctl STDERR\n"
    text += coreinfo.stderr
    text += "\n"
    return text

def subsection(name, text):
    return f"{name}\n\n{text}\n\n\n"


# Log ASAN files
def logasanfiles():
    text = ""
    for f in glob.glob(f'{p.run_dir}/ns-slapd-*san*'):
        with open(f) as asan_report:
            text += subsection(os.path.basename(f), asan_report.read())
    return text


def getReport():
    # Capture data about stoped instances
    # Return a Report (i.e: dict of { 'sectionName': text } )
    # Determine the list of instances
    report = {}
    instancesOK=[]
    instancesKO=[]
    for instdir in DirSrv().list(all=True):
        inst = DirSrv()
        inst.allocate(instdir)
        if inst.status():
            instancesOK.append(inst)
        else:
            instancesKO.append(inst)
    text=""
    # Generate the report
    report["Running instances"] = loglist([i.getServerId() for i in instancesOK])
    report["Stopped instances"] = loglist([i.getServerId() for i in instancesKO])

    # Get core file informations
    report["Core files"] = logcorefiles()

    # Get asan file informations
    report["ASAN files"] = logasanfiles()

    # Get error log informations on stopped servers
    # By default we only log an extract of error log:
    #   Critical, Emergency and standard errors
    #   and the final "server stopped" info line (that denotes a clean stop)
    # But if we detect MDB_MAP_FULL the whole access and error logs are logged
    #  (to help determining how much the map size should be increased)
    for inst in instancesKO:
        path = inst.ds_paths.error_log.format(instance_name=inst.getServerId())
        logFilter = ErrorLogFilter()
        text = log2Report(path, logFilter)
        if '- INFO - main - slapd stopped.' in logFilter.last_line:
            text += logFilter.line
        if logFilter.stop_now:
            report[f"Instance {inst.getServerId()} error log"] = log2Report(path)
            path = inst.ds_paths.access_log.format(instance_name=inst.getServerId())
            report[f"Instance {inst.getServerId()} access log"] = log2Report(path)
        else:
            report[f"extract of instance {inst.getServerId()} error log"] = text

    return report


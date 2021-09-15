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

# Concat filtered lines from a file as a string
# Keeping only relevant data
def logErrors(path):
    keywords = ( "CRIT", "EMERG", "ERR" )
    cleanstop = '- INFO - main - slapd stopped.'
    res=""
    with open(path) as file:
        for line in file:
            if any(ele in line for ele in keywords):
                res += line
        # Check if last line is a clean stop.
        if cleanstop in line:
            res += line
    return res

# Log a list of items
def loglist(list):
    text = ""
    for item in list:
        text += f"	{item}\n"
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

# Log ASAN files
def logasanfiles():
    res = []
    for f in glob.glob(f'{p.run_dir}/ns-slapd-*san*'):
        with open(f) as asan_report:
            res.append((os.path.basename(f), asan_report.read()))
    return res

# Log dbscan -L output (This may help to determine mdb map size)
def logDbscan(inst):
    dblib = inst.get_db_lib()
    dbhome = Paths(inst.getServerId()).db_home_dir
    cmd = [ "dbscan", "-D", dblib, "-L", dbhome ]
    dbscan = subprocess.run(cmd, capture_output=True, shell=False, check=False, text=True)
    text = ""
    text += dbscan.stdout.replace(f'{dbhome}/', '')
    if dbscan.stderr:
        text += "\n\ndbscan STDERR\n"
        text += dbscan.stderr
    text += "\n"
    return text


def getReport():
    # Capture data about stoped instances
    # Return a Report (i.e: list of ( sectionName,  text ) tuple )
    # Lets determine the list of instances
    report = []
    def addSection(name, text):
        report.append((name, text))
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
    # Lets generate the report
    addSection("Running instances", loglist([i.getServerId() for i in instancesOK]))
    addSection("Stopped instances", loglist([i.getServerId() for i in instancesKO]))

    # Get core file informations
    addSection("Core files", logcorefiles())

    # Get asan file informations
    report.extend(logasanfiles())

    # Get error log informations on stopped servers
    # By default we only log an extract of error log:
    #   Critical, Emergency and standard errors
    #   and the final "server stopped" info line (that denotes a clean stop)
    for inst in instancesKO:
        # Log extract of error log
        path = inst.ds_paths.error_log.format(instance_name=inst.getServerId())
        addSection(f"Extract of instance {inst.getServerId()} error log", logErrors(path))
        # And dbscan -L output
        addSection(f"Database info for instance {inst.getServerId()}", logDbscan(inst))

    return report


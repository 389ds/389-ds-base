#nunn --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Will Verify which tasks (Import/Export/Backup/Restore/Reindex (Offline/Online)) may run at the same time
"""

import os
import logging
import pytest
import time
import enum
import shutil
import json
from threading import Thread, get_ident as get_tid
from enum import auto as EnumAuto
from lib389.topologies import topology_st as topo
from lib389.dbgen import dbgen_users
from lib389.backend import Backend
from lib389.properties import ( TASK_WAIT )


#pytestmark = pytest.mark.tier1

NBUSERS=15000 # Should have enough user so that jobs spends at least a few seconds
BASE_SUFFIX="dc=i4585,dc=test"
# result reference file got from version 1.4.2.12
JSONREFNAME = os.path.join(os.path.dirname(__file__), '../data/longduration/db_protect_long_test_reference_1.4.2.12.json')


#Results
OK="OK"
KO="KO"
BUSY="KO"	# So far, no diffrence between failure and failure due to busy

# data associated with both suffixes (i.e DN, bakend name, ldif files, and backup directory )
_suffix1_info={ 'index': 1 }
_suffix2_info={ 'index': 2 }
# Threads result 
_result = {}
# Threads 
_threads = {}

#Mode
OFFLINE="OFFLINE"
ONLINE="ONLINE"

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)



"""
    create suffix bakend, generate ldif, populate the bakend, get a backup
    and initialize suffix_info 
    Note: suffix_info['index'] must be set when calling the function
"""
def _init_suffix(topo, suffix_info):
    index = suffix_info['index']
    # Init suffix_info values
    suffix = f'dc=suffix{index},' + BASE_SUFFIX
    suffix_info['suffix'] = suffix
    ldif_dir = topo.standalone.get_ldif_dir()
    bak_dir = topo.standalone.get_bak_dir()
    suffix_info['name'] = f'suffix{index}'
    suffix_info['rbak'] = bak_dir + f'/r_i4585.bak'  # For archive2db
    suffix_info['wbak'] = bak_dir + f'/w_i4585.bak'  # For db2archive
    suffix_info['rldif'] = ldif_dir + f'/r_suffix{index}.ldif'  # For ldif2db
    suffix_info['wldif'] = ldif_dir + f'/w_suffix{index}.ldif'  # For db2ldif
    # create suffix backend
    be = Backend(topo.standalone)
    be.create(properties={'cn': suffix_info['name'], 'nsslapd-suffix': suffix})
    # Generate rldif ldif file, populate backend, and generate rbak archive
    dbgen_users(topo.standalone, NBUSERS, suffix_info['rldif'], suffix)
    # Populate the backend
    result = _run_ldif2db(topo, ONLINE, suffix_info)
    assert( result == 0 )
    # Generate archive (only second suffix is created)
    if index == 2:
        shutil.rmtree(suffix_info['rbak'], ignore_errors=True)
        result = _job_db2archive(topo, ONLINE, suffix_info['rbak'])
        assert( result == 0 )


"""
    determine json file name
"""
def _get_json_filename(topo):
    return f"{topo.standalone.ds_paths.prefix}/var/log/dirsrv/test_db_protect.json"


"""
    Compare two results pairs
    Note: In the Success + Failure case, do not care about the order 
      because of the threads race
"""
def is_same_result(res1, res2):
    if res1 == res2:
        return True
    if res1 == "OK + KO" and res2 == "KO + OK":
        return True
    if res2 == "OK + KO" and res1 == "KO + OK":
        return True
    return False


"""
    Run a job within a dedicated thread
"""
def _worker(idx, job, topo, mode):
    log.info(f"Thread {idx} id: {get_tid()} started {mode} job {job.__name__}")
    rc0 = None
    rc = None
    try:
        rc = job(topo, mode)
        rc0 = rc
        if mode == ONLINE:
            if rc == 0:
                rc = OK
            else:
                rc = KO
        else:
            if rc:
                rc = OK
            else:
                rc = KO
    except Exception as err:
        log.info(f"Thread {idx} ended {mode} job {job.__name__} with exception {err}")
        log.info(err, exc_info=True)
        rc = KO
    _result[idx] = rc
    log.info(f"Thread {idx} ended {mode} job {job.__name__} with result {rc} (was {rc0})")

"""
    Create a new thread to run a job
"""
def _start_work(*args):
    idx = args[0]
    _threads[idx] = Thread(target=_worker, args=args)
    log.info(f"created Thread {idx} id: {_threads[idx].ident}")
    _result[idx] = None
    _threads[idx].start()


"""
    Wait until thread worker has finished then return the result
"""
def _wait4work(idx):
    _threads[idx].join()
    log.info(f"completed wait on thread {idx} id: {_threads[idx].ident} result is {_result[idx]}")
    return _result[idx]


"""
    Tests all pairs of jobs and check that we got the expected result
        (first job is running in mode1 (ONLINE/OFFLINE)mode)
        (second job is running in mode2 (ONLINE/OFFLINE)mode)
"""
def _check_all_job_pairs(topo, state, mode1, mode2, result):
    """
    Checks all couple of jobs with mode1 online/offline for first job and mode2 for second job
    """
    for idx1, job1 in enumerate(job_list):
        for idx2, job2 in enumerate(job_list):
            log.info(f"Testing {mode1} {job1} + {mode2} {job2}")
            _start_work("job1", job1, topo, mode1)
            # Wait enough to insure job1 is started
            time.sleep(0.5)
            _start_work("job2", job2, topo, mode2)
            res1 = _wait4work("job1")
            res2 = _wait4work("job2")
            key = f"Instance {state}  {mode1} {job1.__name__} + {mode2} {job2.__name__}"
            val = f"{res1} + {res2}"
            result[key] = val
            log.info(f"{key} ==> {val}")


"""
     ********* JOBS DEFINITION **********
"""

def _run_ldif2db(topo, mode, suffix_info):
    if mode == OFFLINE:
        return topo.standalone.ldif2db(suffix_info['name'], None, None, None, suffix_info['rldif'])
    else:
        return topo.standalone.tasks.importLDIF(benamebase=suffix_info['name'], input_file=suffix_info['rldif'], args={TASK_WAIT: True})

def _job_ldif2dbSuffix1(topo, mode):
    return _run_ldif2db(topo, mode, _suffix1_info)
        
def _job_ldif2dbSuffix2(topo, mode):
    return _run_ldif2db(topo, mode, _suffix2_info)


def _run_db2ldif(topo, mode, suffix_info):
    if os.path.exists(suffix_info['wldif']):
        os.remove(suffix_info['wldif'])
    if mode == OFFLINE:
        return topo.standalone.db2ldif(suffix_info['name'], None, None, False, False, suffix_info['wldif'])
    else:
        return topo.standalone.tasks.exportLDIF(benamebase=suffix_info['name'], output_file=suffix_info['wldif'], args={TASK_WAIT: True})

def _job_db2ldifSuffix1(topo, mode):
    return _run_db2ldif(topo, mode, _suffix1_info)
        
def _job_db2ldifSuffix2(topo, mode):
    return _run_db2ldif(topo, mode, _suffix2_info)


def _run_db2index(topo, mode, suffix_info):
    if mode == OFFLINE:
        return topo.standalone.db2index(bename=suffix_info['name'], attrs=['cn'])
    else:
        return topo.standalone.tasks.reindex(topo.standalone, benamebase=suffix_info['name'], attrname='cn', args={TASK_WAIT: True})

def _job_db2indexSuffix1(topo, mode):
    return _run_db2index(topo, mode, _suffix1_info)
        
def _job_db2indexSuffix2(topo, mode):
    return _run_db2index(topo, mode, _suffix2_info)


def _job_db2archive(topo, mode, backup_dir=None):
    # backup is quite fast solets do it several time to increase chance of having concurrent task
    if backup_dir is None:
        backup_dir = _suffix1_info['wbak']
    shutil.rmtree(backup_dir, ignore_errors=True)
    if mode == OFFLINE:
        for i in range(3):
            rc = topo.standalone.db2bak(backup_dir)
            if not rc:
                return False
        return True
    else:
        for i in range(3):
            rc = topo.standalone.tasks.db2bak(backup_dir=backup_dir, args={TASK_WAIT: True})
            if (rc != 0):
                return rc
        return 0

def _job_archive2db(topo, mode, backup_dir=None):
    # restore is quite fast solets do it several time to increase chance of having concurrent task
    if backup_dir is None:
        backup_dir = _suffix1_info['rbak']
    if mode == OFFLINE:
        for i in range(3):
            rc = topo.standalone.bak2db(backup_dir)
            if not rc:
                return False
        return True
    else:
        for i in range(3):
            rc = topo.standalone.tasks.bak2db(backup_dir=backup_dir, args={TASK_WAIT: True})
            if (rc != 0):
                return rc
        return 0

def _job_nothing(topo, mode):
    if mode == OFFLINE:
        return True
    return 0

"""
     ********* END OF JOBS DEFINITION **********
"""


# job_list must be defined after the job get defined
job_list = [ _job_nothing, _job_db2ldifSuffix1, _job_db2ldifSuffix2, _job_ldif2dbSuffix1, _job_ldif2dbSuffix2,
             _job_db2indexSuffix1, _job_db2indexSuffix2, _job_db2archive, _job_archive2db ]



"""
    Beware this test is very long (several hours)
    it checks the results when two task (like import/export/reindex/backup/archive are run at the same time)
    and store the result in a json file 
    the compare with a reference 
"""

def test_db_protect(topo):
    """
    Add an index, then import via cn=tasks

    :id: 462bc550-87d6-11eb-9310-482ae39447e5
    :setup: Standalone Instance
    :steps:
        1. Initialize suffixes
        2. Stop server instance
        3. Compute results for all couples of jobs in OFFLINE,OFFLINE mode
        4. Start server instance
        5. Compute results for all couples of jobs in OFFLINE,OFFLINE mode
        6. Compute results for all couples of jobs in ONLINE,OFFLINE mode
        7. Compute results for all couples of jobs in OFFLINE,ONLINE mode
        8. Compute results for all couples of jobs in ONLINE,ONLINE mode
        9. Store results in log file and json file
        10. Read json reference file
        11. Compute the difference between result and reference
        12. Logs the differences
        13. Assert if differences is not empty

    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Operation successful
        5. Operation successful
        6. Operation successful
        7. Operation successful
        8. Operation successful
        9. Operation successful
        10. Operation successful
        11. Operation successful
        12. Operation successful
        13. Operation successful
    """
    # Step 1: Initialize suffixes
    _init_suffix(topo, _suffix1_info)
    _init_suffix(topo, _suffix2_info)
    result={}
    # Step 2: Stop server instance
    topo.standalone.stop()
    log.info("Server instance is now stopped.")
    # Step 3: Compute results for all couples of jobs in OFFLINE,OFFLINE mode
    _check_all_job_pairs(topo, OFFLINE, OFFLINE, OFFLINE, result)
    # Step 4: Start server instance
    topo.standalone.start()
    log.info("Server instance is now started.")
    # Step 5: Compute results for all couples of jobs in OFFLINE,OFFLINE mode
    _check_all_job_pairs(topo, ONLINE, OFFLINE, OFFLINE, result)
    # Step 6: Compute results for all couples of jobs in ONLINE,OFFLINE mode
    _check_all_job_pairs(topo, ONLINE, ONLINE, OFFLINE, result)
    # Step 7: Compute results for all couples of jobs in OFFLINE,ONLINE mode
    _check_all_job_pairs(topo, ONLINE, OFFLINE, ONLINE, result)
    # Step 8: Compute results for all couples of jobs in ONLINE,ONLINE mode
    _check_all_job_pairs(topo, ONLINE, ONLINE, ONLINE, result)
    # Step 9: Logs the results and store the json file
    for key,val in result.items():
        log.info(f"{key} ==> {val}")
    with open(_get_json_filename(topo), "w") as jfile:
        json.dump(result, jfile)
    # Step 10: read json reference file
    with open(JSONREFNAME, "r") as jfile:
        ref = json.load(jfile)
    # Step 11: Compute the differences
    differences={}
    for key, value in result.items():
        if key in ref:
            if not is_same_result(value, ref[key]):
                differences[key] = ( value, ref[key] )
        else:
            differences[key] = ( value, None )
    for key, value in ref.items():
        if not key in result:
                differences[key] = ( None, value )
    # Step 12: Log the differences
    log.info(f"difference between result an 1.4.2.12 reference are:")
    log.info(f" key:                                                     (result, reference)")
    for key, value in differences.items():
        log.info(f"{key}: {value}")
    # Step 13: assert if there are differences
    assert not differences


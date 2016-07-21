# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
import re
import subprocess
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from datetime import datetime, timedelta


logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


@pytest.fixture(scope="module")
def log_dir(topology):
    '''
    Do a search operation
    and disable access log buffering
    to generate the access log
    '''

    log.info("Diable access log buffering")
    topology.standalone.setAccessLogBuffering(False)

    log.info("Do a ldapsearch operation")
    topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")

    log.info("sleep for sometime so that access log file get generated")
    time.sleep( 1 )

    return topology.standalone.accesslog


def format_time(local_datetime):
    formatted_time = (local_datetime.strftime("[%d/%b/%Y:%H:%M:%S]"))
    return formatted_time


def execute_logconv(start_time_stamp, end_time_stamp, access_log):
    '''
    This function will take start time and end time
    as input parameter and
    assign these values to -S and -E options of logconv
    and, it will execute logconv and return result value
    '''

    log.info("Executing logconv.pl with -S current time and -E end time")
    cmd = ['logconv.pl', '-S', start_time_stamp, '-E', end_time_stamp, access_log]
    log.info(" ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    log.info("standard output" + stdout)
    log.info("standard errors" + stderr)
    return proc.returncode


def test_ticket47910_logconv_start_end_positive(topology, log_dir):
    '''
    Execute logconv.pl with -S and -E(endtime) with random time stamp
    This is execute successfully
    '''
    #
    # Execute logconv.pl -S -E with random timestamp
    #
    log.info('Running test_ticket47910 - Execute logconv.pl -S -E with random values')

    log.info("taking current time with offset of 2 mins and formatting it to feed -S")
    start_time_stamp = (datetime.now() - timedelta(minutes=2))
    formatted_start_time_stamp = format_time(start_time_stamp)

    log.info("taking current time with offset of 2 mins and formatting it to feed -E")
    end_time_stamp = (datetime.now() + timedelta(minutes=2))
    formatted_end_time_stamp = format_time(end_time_stamp)

    log.info("Executing logconv.pl with -S and -E")
    result = execute_logconv(formatted_start_time_stamp, formatted_end_time_stamp, log_dir)
    assert result == 0


def test_ticket47910_logconv_start_end_negative(topology, log_dir):
    '''
    Execute logconv.pl with -S and -E(endtime) with random time stamp
    This is a negative test case, where endtime will be lesser than the
    starttime
    This should give error message
    '''

    #
    # Execute logconv.pl -S and -E with random timestamp
    #
    log.info('Running test_ticket47910 - Execute logconv.pl -S -E with starttime>endtime')

    log.info("taking current time with offset of 2 mins and formatting it to feed -S")
    start_time_stamp = (datetime.now() + timedelta(minutes=2))
    formatted_start_time_stamp = format_time(start_time_stamp)

    log.info("taking current time with offset of 2 mins and formatting it to feed -E")
    end_time_stamp = (datetime.now() - timedelta(minutes=2))
    formatted_end_time_stamp = format_time(end_time_stamp)

    log.info("Executing logconv.pl with -S and -E")
    result = execute_logconv(formatted_start_time_stamp, formatted_end_time_stamp, log_dir)
    assert result == 1


def test_ticket47910_logconv_start_end_invalid(topology, log_dir):
    '''
    Execute logconv.pl with -S and -E(endtime) with invalid time stamp
    This is a negative test case, where it should give error message
    '''
    #
    # Execute logconv.pl -S and -E with invalid timestamp
    #
    log.info('Running test_ticket47910 - Execute logconv.pl -S -E with invalid timestamp')
    log.info("Set start time and end time to invalid values")
    start_time_stamp = "invalid"
    end_time_stamp = "invalid"

    log.info("Executing logconv.pl with -S and -E")
    result = execute_logconv(start_time_stamp, end_time_stamp, log_dir)
    assert result == 1


def test_ticket47910_logconv_noaccesslogs(topology, log_dir):

    '''
    Execute logconv.pl -S(starttime) without specify
    access logs location
    '''

    #
    # Execute logconv.pl -S with random timestamp and no access log location
    #
    log.info('Running test_ticket47910 - Execute logconv.pl without access logs')

    log.info("taking current time with offset of 2 mins and formatting it to feed -S")
    time_stamp = (datetime.now() - timedelta(minutes=2))
    formatted_time_stamp = format_time(time_stamp)
    log.info("Executing logconv.pl with -S current time")
    cmd = ['logconv.pl', '-S', formatted_time_stamp]
    log.info(" ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    log.info("standard output" + stdout)
    log.info("standard errors" + stderr)

    assert proc.returncode == 1


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    pytest.main("-s ticket47910_test.py")

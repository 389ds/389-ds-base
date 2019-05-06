# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import subprocess
from datetime import datetime, timedelta

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import SUFFIX, DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]


@pytest.fixture(scope="module")
def log_dir(topology_st):
    '''
    Do a search operation
    and disable access log buffering
    to generate the access log
    '''

    log.info("Diable access log buffering")
    topology_st.standalone.setAccessLogBuffering(False)

    log.info("Do a ldapsearch operation")
    topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")

    log.info("sleep for sometime so that access log file get generated")
    time.sleep(1)

    return topology_st.standalone.accesslog


def format_time(local_datetime):
    formatted_time = (local_datetime.strftime("[%d/%b/%Y:%H:%M:%S]"))
    return formatted_time


def execute_logconv(inst, start_time_stamp, end_time_stamp, access_log):
    '''
    This function will take start time and end time
    as input parameter and
    assign these values to -S and -E options of logconv
    and, it will execute logconv and return result value
    '''

    log.info("Executing logconv.pl with -S current time and -E end time")
    cmd = [os.path.join(inst.get_bin_dir(), 'logconv.pl'), '-S', start_time_stamp, '-E', end_time_stamp, access_log]
    log.info(" ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    log.info("standard output" + ensure_str(stdout))
    log.info("standard errors" + ensure_str(stderr))
    return proc.returncode


def test_ticket47910_logconv_start_end_positive(topology_st, log_dir):
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
    result = execute_logconv(topology_st.standalone, formatted_start_time_stamp, formatted_end_time_stamp, log_dir)
    assert result == 0


def test_ticket47910_logconv_start_end_negative(topology_st, log_dir):
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
    result = execute_logconv(topology_st.standalone, formatted_start_time_stamp, formatted_end_time_stamp, log_dir)
    assert result == 1


def test_ticket47910_logconv_start_end_invalid(topology_st, log_dir):
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
    result = execute_logconv(topology_st.standalone, start_time_stamp, end_time_stamp, log_dir)
    assert result == 1


def test_ticket47910_logconv_noaccesslogs(topology_st, log_dir):
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
    cmd = [os.path.join(topology_st.standalone.get_bin_dir(), 'logconv.pl'), '-S', formatted_time_stamp]
    log.info(" ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    log.info("standard output" + ensure_str(stdout))
    log.info("standard errors" + ensure_str(stderr))

    assert proc.returncode == 1


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    pytest.main("-s ticket47910_test.py")

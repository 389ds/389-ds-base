# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


import os
import subprocess
import re
import time
import pytest
from lib389.tasks import *
from lib389._constants import *
from lib389.utils import ensure_bytes
from lib389.backend import Backends
from lib389.topologies import topology_st as topo
from lib389.paths import *
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier2
disk_monitoring_ack = pytest.mark.skipif(not os.environ.get('DISK_MONITORING_ACK', False), reason="Disk monitoring tests may damage system configuration.")

THRESHOLD = '30'
THRESHOLD_BYTES = '30000000'


def _withouterrorlog(topo, condition, maxtimesleep):
    timecount = 0
    while eval(condition):
        time.sleep(1)
        timecount += 1
        if timecount >= maxtimesleep: break
    assert not eval(condition)


def _witherrorlog(topo, condition, maxtimesleep):
    timecount = 0
    with open(topo.standalone.errlog, 'r') as study: study = study.read()
    while condition not in study:
        time.sleep(1)
        timecount += 1
        with open(topo.standalone.errlog, 'r') as study: study = study.read()
        if timecount >= maxtimesleep: break
    assert condition in study


def presetup(topo):
    """
    This is function is part of fixture function setup , will setup the environment for this test.
    """
    topo.standalone.stop()
    if os.path.exists(topo.standalone.ds_paths.log_dir):
        subprocess.call(['mount', '-t', 'tmpfs', '-o', 'size=35M', 'tmpfs', topo.standalone.ds_paths.log_dir])
    else:
        os.mkdir(topo.standalone.ds_paths.log_dir)
        subprocess.call(['mount', '-t', 'tmpfs', '-o', 'size=35M', 'tmpfs', topo.standalone.ds_paths.log_dir])
    subprocess.call('chown {}: -R {}'.format(DEFAULT_USER, topo.standalone.ds_paths.log_dir), shell=True)
    subprocess.call('chown {}: -R {}/*'.format(DEFAULT_USER, topo.standalone.ds_paths.log_dir), shell=True)
    subprocess.call('restorecon -FvvR {}'.format(topo.standalone.ds_paths.log_dir), shell=True)
    topo.standalone.start()


def setupthesystem(topo):
    """
    This function is part of fixture function setup , will setup the environment for this test.
    """
    global TOTAL_SIZE, USED_SIZE, AVAIL_SIZE, HALF_THR_FILL_SIZE, FULL_THR_FILL_SIZE
    topo.standalone.start()
    topo.standalone.config.set('nsslapd-disk-monitoring-grace-period', '1')
    topo.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')
    topo.standalone.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(THRESHOLD_BYTES))
    TOTAL_SIZE = int(re.findall(r'\d+', str(os.statvfs(topo.standalone.ds_paths.log_dir)))[2])*4096/1024/1024
    AVAIL_SIZE = round(int(re.findall(r'\d+', str(os.statvfs(topo.standalone.ds_paths.log_dir)))[3]) * 4096 / 1024 / 1024)
    USED_SIZE = TOTAL_SIZE - AVAIL_SIZE
    HALF_THR_FILL_SIZE = TOTAL_SIZE - float(THRESHOLD) + 5 - USED_SIZE
    FULL_THR_FILL_SIZE = TOTAL_SIZE - 0.5 * float(THRESHOLD) + 5 - USED_SIZE
    HALF_THR_FILL_SIZE = round(HALF_THR_FILL_SIZE)
    FULL_THR_FILL_SIZE = round(FULL_THR_FILL_SIZE)
    topo.standalone.restart()


@pytest.fixture(scope="module")
def setup(request, topo):
    """
    This is the fixture function , will run before running every test case.
    """
    presetup(topo)
    setupthesystem(topo)

    def fin():
        topo.standalone.stop()
        subprocess.call(['umount', '-fl', topo.standalone.ds_paths.log_dir])
        topo.standalone.start()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def reset_logs(topo):
    """
    Reset the errors log file before the test
    """
    open('{}/errors'.format(topo.standalone.ds_paths.log_dir), 'w').close()


@disk_monitoring_ack
def test_verify_operation_when_disk_monitoring_is_off(topo, setup, reset_logs):
    """Verify operation when Disk monitoring is off

    :id: 73a97536-fe9e-11e8-ba9f-8c16451d917b
    :setup: Standalone
    :steps:
        1. Turn off disk monitoring
        2. Go below the threshold
        3. Check DS is up and not entering shutdown mode
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
    """
    try:
        # Turn off disk monitoring
        topo.standalone.config.set('nsslapd-disk-monitoring', 'off')
        topo.standalone.restart()
        # go below the threshold
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo1'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
        # Wait for disk monitoring plugin thread to wake up
        _withouterrorlog(topo, 'topo.standalone.status() != True', 10)
        # Check DS is up and not entering shutdown mode
        assert topo.standalone.status() == True
    finally:
        os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))
        os.remove('{}/foo1'.format(topo.standalone.ds_paths.log_dir))


@disk_monitoring_ack
def test_free_up_the_disk_space_and_change_ds_config(topo, setup, reset_logs):
    """Free up the disk space and change DS config

    :id: 7be4d560-fe9e-11e8-a307-8c16451d917b
    :setup: Standalone
    :steps:
        1. Enabling Disk Monitoring plugin and setting disk monitoring logging to critical
        2. Verify no message about loglevel is present in the error log
        3. Verify no message about disabling logging is present in the error log
        4. Verify no message about removing rotated logs is present in the error log
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
        4. Should Success
    """
    # Enabling Disk Monitoring plugin and setting disk monitoring logging to critical
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    topo.standalone.restart()
    # Verify no message about loglevel is present in the error log
    # Verify no message about disabling logging is present in the error log
    # Verify no message about removing rotated logs is present in the error log
    with open(topo.standalone.errlog, 'r') as study: study = study.read()
    assert 'temporarily setting error loglevel to zero' not in study
    assert 'disabling access and audit logging' not in study
    assert 'deleting rotated logs' not in study


@disk_monitoring_ack
def test_verify_operation_with_nsslapd_disk_monitoring_logging_critical_off(topo, setup, reset_logs):
    """Verify operation with "nsslapd-disk-monitoring-logging-critical: off

    :id: 82363bca-fe9e-11e8-9ae7-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that verbose logging was set to default level
        2. Verify that logging is disabled
        3. Verify that rotated logs were not removed
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
    """
    try:
        # Verify that verbose logging was set to default level
        assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
        assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
        topo.standalone.restart()
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(HALF_THR_FILL_SIZE)])
        _witherrorlog(topo, 'temporarily setting error loglevel to the default level', 11)
        assert LOG_DEFAULT == int(re.findall(r'nsslapd-errorlog-level: \d+', str(
            topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-errorlog-level'])))[
                                      0].split(' ')[1])
        # Verify that logging is disabled
        _withouterrorlog(topo, "topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'off'", 10)
        assert topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') == 'off'
        # Verify that rotated logs were not removed
        with open(topo.standalone.errlog, 'r') as study: study = study.read()
        assert 'disabling access and audit logging' in study
        _witherrorlog(topo, 'deleting rotated logs', 11)
        study = open(topo.standalone.errlog).read()
        assert "Unable to remove file: {}".format(topo.standalone.ds_paths.log_dir) not in study
        assert 'is too far below the threshold' not in study
    finally:
        os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_on_below_half_of_the_threshold(topo, setup, reset_logs):
    """Verify operation with \"nsslapd-disk-monitoring-logging-critical: on\" below 1/2 of the threshold
    Verify recovery

    :id: 8940c502-fe9e-11e8-bcc0-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that DS goes into shutdown mode
        2. Verify that DS exited shutdown mode
    :expectedresults:
        1. Should Success
        2. Should Success
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    topo.standalone.restart()
    # Verify that DS goes into shutdown mode
    if float(THRESHOLD) > FULL_THR_FILL_SIZE:
        FULL_THR_FILL_SIZE_new = FULL_THR_FILL_SIZE + round(float(THRESHOLD) - FULL_THR_FILL_SIZE) + 1
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE_new)])
    else:
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
    _witherrorlog(topo, 'is too far below the threshold', 20)
    os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))
    # Verify that DS exited shutdown mode
    _witherrorlog(topo, 'Available disk space is now acceptable', 25)


@disk_monitoring_ack
def test_setting_nsslapd_disk_monitoring_logging_critical_to_off(topo, setup, reset_logs):
    """Setting nsslapd-disk-monitoring-logging-critical to "off"

    :id: 93265ec4-fe9e-11e8-af93-8c16451d917b
    :setup: Standalone
    :steps:
        1. Setting nsslapd-disk-monitoring-logging-critical to "off"
    :expectedresults:
        1. Should Success
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    topo.standalone.restart()
    assert topo.standalone.status() == True


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_off(topo, setup, reset_logs):
    """Verify operation with nsslapd-disk-monitoring-logging-critical: off

    :id: 97985a52-fe9e-11e8-9914-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that logging is disabled
        2. Verify that rotated logs were removed
        3. Verify that verbose logging was set to default level
        4. Verify that logging is disabled
        5. Verify that rotated logs were removed
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
        4. Should Success
        5. Should Success
    """
    # Verify that logging is disabled
    try:
        assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
        assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
        assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
        assert topo.standalone.config.set('nsslapd-accesslog-maxlogsize', '1')
        assert topo.standalone.config.set('nsslapd-accesslog-logrotationtimeunit', 'minute')
        assert topo.standalone.config.set('nsslapd-accesslog-level', '772')
        topo.standalone.restart()
        # Verify that rotated logs were removed
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
        for i in range(10):
            user_properties = {
                'uid': 'cn=anuj{}'.format(i),
                'cn': 'cn=anuj{}'.format(i),
                'sn': 'cn=anuj{}'.format(i),
                'userPassword': "Itsme123",
                'uidNumber': '1{}'.format(i),
                'gidNumber': '2{}'.format(i),
                'homeDirectory': '/home/{}'.format(i)
            }
            users.create(properties=user_properties)
        for j in range(100):
            for i in [i for i in users.list()]: i.bind('Itsme123')
        assert re.findall(r'access.\d+-\d+',str(os.listdir(topo.standalone.ds_paths.log_dir)))
        topo.standalone.bind_s(DN_DM, PW_DM)
        assert topo.standalone.config.set('nsslapd-accesslog-maxlogsize', '100')
        assert topo.standalone.config.set('nsslapd-accesslog-logrotationtimeunit', 'day')
        assert topo.standalone.config.set('nsslapd-accesslog-level', '256')
        topo.standalone.restart()
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo2'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(HALF_THR_FILL_SIZE)])
        # Verify that verbose logging was set to default level
        _witherrorlog(topo, 'temporarily setting error loglevel to the default level', 10)
        assert LOG_DEFAULT == int(re.findall(r'nsslapd-errorlog-level: \d+', str(
            topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-errorlog-level'])))[0].split(' ')[1])
        # Verify that logging is disabled
        _withouterrorlog(topo, "topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'off'", 20)
        with open(topo.standalone.errlog, 'r') as study: study = study.read()
        assert 'disabling access and audit logging' in study
        # Verify that rotated logs were removed
        _witherrorlog(topo, 'deleting rotated logs', 10)
        with open(topo.standalone.errlog, 'r') as study:study = study.read()
        assert 'Unable to remove file:' not in study
        assert 'is too far below the threshold' not in study
        for i in [i for i in users.list()]: i.delete()
    finally:
        os.remove('{}/foo2'.format(topo.standalone.ds_paths.log_dir))


@disk_monitoring_ack
def test_operation_with_nsslapd_disk_monitoring_logging_critical_off_below_half_of_the_threshold(topo, setup, reset_logs):
    """Verify operation with nsslapd-disk-monitoring-logging-critical: off below 1/2 of the threshold
    Verify shutdown
    Recovery and setup

    :id: 9d4c7d48-fe9e-11e8-b5d6-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that DS goes into shutdown mode
        2. Verifying that DS has been shut down after the grace period
        3. Verify logging enabled
        4. Create rotated logfile
        5. Enable verbose logging
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
        4. Should Success
        5. Should Success
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
    topo.standalone.restart()
    # Verify that DS goes into shutdown mode
    if float(THRESHOLD) > FULL_THR_FILL_SIZE:
        FULL_THR_FILL_SIZE_new = FULL_THR_FILL_SIZE + round(float(THRESHOLD) - FULL_THR_FILL_SIZE)
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE_new)])
    else:
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
    # Increased sleep to avoid failure
    _witherrorlog(topo, 'is too far below the threshold', 100)
    _witherrorlog(topo, 'Signaling slapd for shutdown', 90)
    # Verifying that DS has been shut down after the grace period
    time.sleep(2)
    assert topo.standalone.status() == False
    # free_space
    os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))
    open('{}/errors'.format(topo.standalone.ds_paths.log_dir), 'w').close()
    # StartSlapd
    topo.standalone.start()
    # verify logging enabled
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') == 'on'
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-errorlog-logging-enabled') == 'on'
    with open(topo.standalone.errlog, 'r') as study: study = study.read()
    assert 'disabling access and audit logging' not in study
    assert topo.standalone.config.set('nsslapd-accesslog-maxlogsize', '1')
    assert topo.standalone.config.set('nsslapd-accesslog-logrotationtimeunit', 'minute')
    assert topo.standalone.config.set('nsslapd-accesslog-level', '772')
    topo.standalone.restart()
    # create rotated logfile
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(10):
        user_properties = {
            'uid': 'cn=anuj{}'.format(i),
            'cn': 'cn=anuj{}'.format(i),
            'sn': 'cn=anuj{}'.format(i),
            'userPassword': "Itsme123",
            'uidNumber': '1{}'.format(i),
            'gidNumber': '2{}'.format(i),
            'homeDirectory': '/home/{}'.format(i)
        }
        users.create(properties=user_properties)
    for j in range(100):
        for i in [i for i in users.list()]: i.bind('Itsme123')
    assert re.findall(r'access.\d+-\d+',str(os.listdir(topo.standalone.ds_paths.log_dir)))
    topo.standalone.bind_s(DN_DM, PW_DM)
    # enable verbose logging
    assert topo.standalone.config.set('nsslapd-accesslog-maxlogsize', '100')
    assert topo.standalone.config.set('nsslapd-accesslog-logrotationtimeunit', 'day')
    assert topo.standalone.config.set('nsslapd-accesslog-level', '256')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    topo.standalone.restart()
    for i in [i for i in users.list()]: i.delete()


@disk_monitoring_ack
def test_go_straight_below_half_of_the_threshold(topo, setup, reset_logs):
    """Go straight below 1/2 of the threshold
    Recovery and setup

    :id: a2a0664c-fe9e-11e8-b220-8c16451d917b
    :setup: Standalone
    :steps:
        1. Go straight below 1/2 of the threshold
        2. Verify that verbose logging was set to default level
        3. Verify that logging is disabled
        4. Verify DS is in shutdown mode
        5. Verify DS has recovered from shutdown
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
        4. Should Success
        5. Should Success
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'off')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    topo.standalone.restart()
    if float(THRESHOLD) > FULL_THR_FILL_SIZE:
        FULL_THR_FILL_SIZE_new = FULL_THR_FILL_SIZE + round(float(THRESHOLD) - FULL_THR_FILL_SIZE) + 1
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE_new)])
    else:
        subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
    _witherrorlog(topo, 'temporarily setting error loglevel to the default level', 11)
    # Verify that verbose logging was set to default level
    assert LOG_DEFAULT == int(re.findall(r'nsslapd-errorlog-level: \d+',
                                                str(topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE,
                                                                             '(objectclass=*)',
                                                                             ['nsslapd-errorlog-level']))
                                                )[0].split(' ')[1])
    # Verify that logging is disabled
    _withouterrorlog(topo, "topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'off'", 11)
    # Verify that rotated logs were removed
    _witherrorlog(topo, 'disabling access and audit logging', 2)
    _witherrorlog(topo, 'deleting rotated logs', 11)
    with open(topo.standalone.errlog, 'r') as study:study = study.read()
    assert 'Unable to remove file:' not in study
    # Verify DS is in shutdown mode
    _withouterrorlog(topo, 'topo.standalone.status() != False', 90)
    _witherrorlog(topo, 'is too far below the threshold', 2)
    # Verify DS has recovered from shutdown
    os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))
    open('{}/errors'.format(topo.standalone.ds_paths.log_dir), 'w').close()
    topo.standalone.start()
    _withouterrorlog(topo, "topo.standalone.config.get_attr_val_utf8('nsslapd-accesslog-logging-enabled') != 'on'", 20)
    with open(topo.standalone.errlog, 'r') as study: study = study.read()
    assert 'disabling access and audit logging' not in study


@disk_monitoring_ack
def test_readonly_on_threshold(topo, setup, reset_logs):
    """Verify that nsslapd-disk-monitoring-readonly-on-threshold switches the server to read-only mode

    :id: 06814c19-ef3c-4800-93c9-c7c6e76fcbb9
    :setup: Standalone
    :steps:
        1. Verify that the backend is in read-only mode
        2. Go back above the threshold
        3. Verify that the backend is in read-write mode
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
    """
    file_path = '{}/foo'.format(topo.standalone.ds_paths.log_dir)
    backends = Backends(topo.standalone)
    backend_name = backends.list()[0].rdn
    # Verify that verbose logging was set to default level
    topo.standalone.deleteErrorLogs()
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-readonly-on-threshold', 'on')
    topo.standalone.restart()
    try:
        subprocess.call(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1M', f'count={HALF_THR_FILL_SIZE}'])
        _witherrorlog(topo, f"Putting the backend '{backend_name}' to read-only mode", 11)
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
        try:
            user = users.create_test_user()
            user.delete()
        except ldap.UNWILLING_TO_PERFORM as e:
            if 'database is read-only' not in str(e):
                raise
        os.remove(file_path)
        _witherrorlog(topo, f"Putting the backend '{backend_name}' back to read-write mode", 11)
        user = users.create_test_user()
        assert user.exists()
        user.delete()
    finally:
        if os.path.exists(file_path):
            os.remove(file_path)


@disk_monitoring_ack
def test_readonly_on_threshold_below_half_of_the_threshold(topo, setup, reset_logs):
    """Go below 1/2 of the threshold when readonly on threshold is enabled

    :id: 10262663-b41f-420e-a2d0-9532dd54fa7c
    :setup: Standalone
    :steps:
    :expectedresults:
        1. Go straight below 1/2 of the threshold
        2. Verify that the backend is in read-only mode
        3. Go back above the threshold
        4. Verify that the backend is in read-write mode
    :expectedresults:
        1. Should Success
        2. Should Success
        3. Should Success
        4. Should Success
    """
    file_path = '{}/foo'.format(topo.standalone.ds_paths.log_dir)
    backends = Backends(topo.standalone)
    backend_name = backends.list()[0].rdn
    topo.standalone.deleteErrorLogs()
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-readonly-on-threshold', 'on')
    topo.standalone.restart()
    try:
        if float(THRESHOLD) > FULL_THR_FILL_SIZE:
            FULL_THR_FILL_SIZE_new = FULL_THR_FILL_SIZE + round(float(THRESHOLD) - FULL_THR_FILL_SIZE) + 1
            subprocess.call(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1M', f'count={FULL_THR_FILL_SIZE_new}'])
        else:
            subprocess.call(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1M', f'count={FULL_THR_FILL_SIZE}'])
        _witherrorlog(topo, f"Putting the backend '{backend_name}' to read-only mode", 11)
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
        try:
            user = users.create_test_user()
            user.delete()
        except ldap.UNWILLING_TO_PERFORM as e:
            if 'database is read-only' not in str(e):
                raise
        _witherrorlog(topo, 'is too far below the threshold', 51)
        # Verify DS has recovered from shutdown
        os.remove(file_path)
        _witherrorlog(topo, f"Putting the backend '{backend_name}' back to read-write mode", 51)
        user = users.create_test_user()
        assert user.exists()
        user.delete()
    finally:
        if os.path.exists(file_path):
            os.remove(file_path)


@disk_monitoring_ack
def test_below_half_of_the_threshold_not_starting_after_shutdown(topo, setup, reset_logs):
    """Test that the instance won't start if we are below 1/2 of the threshold

    :id: cceeaefd-9fa4-45c5-9ac6-9887a0671ef8
    :setup: Standalone
    :steps:
        1. Go straight below 1/2 of the threshold
        2. Try to start the instance
        3. Go back above the threshold
        4. Try to start the instance
    :expectedresults:
        1. Should Success
        2. Should Fail
        3. Should Success
        4. Should Success
    """
    file_path = '{}/foo'.format(topo.standalone.ds_paths.log_dir)
    topo.standalone.deleteErrorLogs()
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    topo.standalone.restart()
    try:
        if float(THRESHOLD) > FULL_THR_FILL_SIZE:
            FULL_THR_FILL_SIZE_new = FULL_THR_FILL_SIZE + round(float(THRESHOLD) - FULL_THR_FILL_SIZE) + 1
            subprocess.call(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1M', f'count={FULL_THR_FILL_SIZE_new}'])
        else:
            subprocess.call(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1M', f'count={FULL_THR_FILL_SIZE}'])
        _withouterrorlog(topo, 'topo.standalone.status() == True', 120)
        try:
            topo.standalone.start()
        except (ValueError, subprocess.CalledProcessError):
            topo.standalone.log.info("Instance start up has failed as expected")
        _witherrorlog(topo, f'is too far below the threshold({THRESHOLD_BYTES} bytes). Exiting now', 2)
        # Verify DS has recovered from shutdown
        os.remove(file_path)
        topo.standalone.start()
    finally:
        if os.path.exists(file_path):
            os.remove(file_path)


@disk_monitoring_ack
def test_go_straight_below_4kb(topo, setup, reset_logs):
    """Go straight below 4KB

    :id: a855115a-fe9e-11e8-8e91-8c16451d917b
    :setup: Standalone
    :steps:
        1. Go straight below 4KB
        2. Clean space
    :expectedresults:
        1. Should Success
        2. Should Success
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    topo.standalone.restart()
    subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
    subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo1'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(FULL_THR_FILL_SIZE)])
    _withouterrorlog(topo, 'topo.standalone.status() != False', 11)
    os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))
    os.remove('{}/foo1'.format(topo.standalone.ds_paths.log_dir))
    topo.standalone.start()
    assert topo.standalone.status() == True


@disk_monitoring_ack
@pytest.mark.bz982325
def test_threshold_to_overflow_value(topo, setup, reset_logs):
    """Overflow in nsslapd-disk-monitoring-threshold

    :id: ad60ab3c-fe9e-11e8-88dc-8c16451d917b
    :setup: Standalone
    :steps:
        1. Setting nsslapd-disk-monitoring-threshold to overflow_value
    :expectedresults:
        1. Should Success
    """
    overflow_value = '3000000000'
    # Setting nsslapd-disk-monitoring-threshold to overflow_value
    assert topo.standalone.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(overflow_value))
    assert overflow_value == re.findall(r'nsslapd-disk-monitoring-threshold: \d+', str(
        topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)',
                                 ['nsslapd-disk-monitoring-threshold'])))[0].split(' ')[1]


@disk_monitoring_ack
@pytest.mark.bz970995
def test_threshold_is_reached_to_half(topo, setup, reset_logs):
    """RHDS not shutting down when disk monitoring threshold is reached to half.

    :id: b2d3665e-fe9e-11e8-b9c0-8c16451d917b
    :setup: Standalone
    :steps: Standalone
        1. Verify that there is not endless loop of error messages
    :expectedresults:
        1. Should Success
    """

    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-threshold', ensure_bytes(THRESHOLD_BYTES))
    topo.standalone.restart()
    subprocess.call(['dd', 'if=/dev/zero', 'of={}/foo'.format(topo.standalone.ds_paths.log_dir), 'bs=1M', 'count={}'.format(HALF_THR_FILL_SIZE)])
    # Verify that there is not endless loop of error messages
    _witherrorlog(topo, "temporarily setting error loglevel to the default level", 10)
    with open(topo.standalone.errlog, 'r') as study:study = study.read()
    assert len(re.findall("temporarily setting error loglevel to the default level", study)) == 1
    os.remove('{}/foo'.format(topo.standalone.ds_paths.log_dir))


@disk_monitoring_ack
@pytest.mark.parametrize("test_input,expected", [
    ("nsslapd-disk-monitoring-threshold", '-2'),
    ("nsslapd-disk-monitoring-threshold", '9223372036854775808'),
    ("nsslapd-disk-monitoring-threshold", '2047'),
    ("nsslapd-disk-monitoring-threshold", '0'),
    ("nsslapd-disk-monitoring-threshold", '-1294967296'),
    ("nsslapd-disk-monitoring-threshold", 'invalid'),
    ("nsslapd-disk-monitoring", 'invalid'),
    ("nsslapd-disk-monitoring", '1'),
    ("nsslapd-disk-monitoring-grace-period", '0'),
    ("nsslapd-disk-monitoring-grace-period", '525 948'),
    ("nsslapd-disk-monitoring-grace-period", '-1'),
    ("nsslapd-disk-monitoring-logging-critical", 'oninvalid'),
    ("nsslapd-disk-monitoring-grace-period", '-1'),
    ("nsslapd-disk-monitoring-grace-period", '0'),
])
def test_negagtive_parameterize(topo, setup, reset_logs, test_input, expected):
    """Verify that invalid operations are not permitted

    :id: b88efbf8-fe9e-11e8-8499-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Verify that invalid operations are not permitted.
    :expectedresults:
        1. Should not success.
    """
    with pytest.raises(Exception):
        topo.standalone.config.set(test_input, ensure_bytes(expected))


@disk_monitoring_ack
def test_valid_operations_are_permitted(topo, setup, reset_logs):
    """Verify that valid operations are  permitted

    :id: bd4f83f6-fe9e-11e8-88f4-8c16451d917b
    :setup: Standalone
    :steps:
        1. Verify that valid operations are  permitted
    :expectedresults:
        1. Should Success.
    """
    assert topo.standalone.config.set('nsslapd-disk-monitoring', 'on')
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'on')
    assert topo.standalone.config.set('nsslapd-errorlog-level', '8')
    topo.standalone.restart()
    # Trying to delete nsslapd-disk-monitoring-threshold
    assert topo.standalone.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-threshold', '')])
    # Trying to add another value to nsslapd-disk-monitoring-threshold (check that it is not multivalued)
    topo.standalone.config.add('nsslapd-disk-monitoring-threshold', '2000001')
    # Trying to delete nsslapd-disk-monitoring
    assert topo.standalone.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring', ensure_bytes(str(
        topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectclass=*)', ['nsslapd-disk-monitoring'])[
            0]).split(' ')[2].split('\n\n')[0]))])
    # Trying to add another value to nsslapd-disk-monitoring
    topo.standalone.config.add('nsslapd-disk-monitoring', 'off')
    # Trying to delete nsslapd-disk-monitoring-grace-period
    assert topo.standalone.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-grace-period', '')])
    # Trying to add another value to nsslapd-disk-monitoring-grace-period
    topo.standalone.config.add('nsslapd-disk-monitoring-grace-period', '61')
    # Trying to delete nsslapd-disk-monitoring-logging-critical
    assert topo.standalone.modify_s('cn=config', [(ldap.MOD_DELETE, 'nsslapd-disk-monitoring-logging-critical',
                                                       ensure_bytes(str(
                                                           topo.standalone.search_s('cn=config', ldap.SCOPE_SUBTREE,
                                                                                    '(objectclass=*)', [
                                                                                        'nsslapd-disk-monitoring-logging-critical'])[
                                                               0]).split(' ')[2].split('\n\n')[0]))])
    # Trying to add another value to nsslapd-disk-monitoring-logging-critical
    assert topo.standalone.config.set('nsslapd-disk-monitoring-logging-critical', 'on')


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import os
import logging
import pwd
import pytest
import re
import socket
import subprocess
import tarfile
import time
from contextlib import suppress
from itertools import permutations
from lib389 import DirSrv
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.dblib import get_bdb_impl_status, dblib_bdb2mdb
from lib389.config import Config
from lib389.dseldif import DSEldif
from lib389.idm.services import ServiceAccounts
from lib389.nss_ssl import NssSsl
from lib389.passwd import password_generate
from lib389.replica import ReplicationManager, Replicas
from lib389.topologies import topology_m2 as topo_m2
from lib389.utils import (
        get_default_db_lib,
        escapeDNFiltValue,
        resolve_selinux_path,
        selinux_label_file,
        selinux_label_port,
        selinux_present,
        )
from pathlib import Path

log = logging.getLogger(__name__)
DEBUGGING = os.getenv("DEBUGGING", default=False)

# Environmaent variable used to generate the tarball
GENTARBALL = "GENERATE_BDB_TARBALL"
CURRENT_FILE = os.path.realpath(__file__)
TARFILENAME = f'{Path(CURRENT_FILE).parents[2]}/data/bdb_instances/instances.tgz'

class MigrationHandler:
    def __init__(self):
        self.hostname = socket.gethostname()
        self.uid = os.getuid()
        self.gid = os.getgid()
        self.idir = os.getenv("PREFIX", "/")
        self.inst_list=[]
        self.username = pwd.getpwuid(self.uid).pw_name
        # Determine the instances names
        with tarfile.open(name=TARFILENAME, mode='r:gz') as tar:
            names = []
            for member in tar:
                match = re.match('.*/slapd-([^/]*)/dse.ldif$',member.path)
                if match and match.group(1) not in names:
                    names.append(match.group(1))
            self.names = names
            log.info(f'MigrationHandler has following instances: {names}')


    def replace_dse_line(self, line, old_hostname):
        if old_hostname:
            line = line.replace(old_hostname, self.hostname)
        if self.idir != '/':
            for name in ( '/etc/', '/var/l', '/run/', ):
                line = line.replace(f' {name}', f' {self.idir}/{name}')
        if line.startswith('nsslapd-localuser:'):
            line = f'nsslapd-localuser: {self.username}\n'
        return line

    def remap_dse(self, path):
        old_hostname = None
        tmppath = f'{path}.tmp'
        with open(path, 'rt') as fin:
            with open(tmppath, 'wt') as fout:
                for line in fin:
                    match = re.match(r'nsslapd-localhost:\s*(\S*)',line)
                    if match:
                        old_hostname = match.group(1)
                    fout.write(self.replace_dse_line(line, old_hostname))
        os.remove(path)
        os.rename(tmppath, path)

    def tar_filter(self, member, path):
        fullpath = f'{path}/{member.path}'
        # Do not try to overwrite existing files except dse.ldif
        if os.path.exists(fullpath) and not member.path.endswith('/dse.ldif'):
            return None
        # use the current user credentials
        member.uid = self.uid
        member.gid = self.gid
        # Security: reject tricky files that may be in tar ball
        return tarfile.tar_filter(member, path)

    def relabel_selinux(self):
        if not selinux_present():
            return
        for inst in self.inst_list:
            log.info("Performing SELinux labeling on instance {inst.serverid} ...")
            dse = DSEldif(inst)

            selinux_attr_labels = {
                (DN_CONFIG, 'nsslapd-bakdir'): 'dirsrv_var_lib_t',
                (DN_CONFIG, 'nsslapd-certdir'): 'dirsrv_config_t',
                (DN_CONFIG, 'nsslapd-ldifdir'): 'dirsrv_var_lib_t',
                (DN_CONFIG, 'nsslapd-lockdir'): 'dirsrv_var_lock_t',
                (DN_CONFIG, 'nsslapd-rundir'): 'dirsrv_var_run_t',
                (DN_CONFIG, 'nsslapd-schemadir'): 'dirsrv_config_t',
                (DN_CONFIG, 'nsslapd-tmpdir'): 'tmp_t',
                (DN_CONFIG_LDBM, 'nsslapd-db-home-directory'): 'dirsrv_tmpfs_t',
                (DN_CONFIG_LDBM, 'nsslapd-directory'): 'dirsrv_var_lib_t',
            }
            # Generates the path -> label dict
            selinux_labels = { dse.get(pair[0], pair[1], single=True) : label for pair,label in selinux_attr_labels.items() }
            log_dir = os.path.dirname(dse.get(DN_CONFIG, 'nsslapd-accesslog', single=True))
            selinux_attr_labels[log_dir] = 'dirsrv_var_log_t'
            if os.path.isdir('/run/dirsrv'):
                selinux_attr_labels['/run/dirsrv'] = 'dirsrv_var_run_t'

            for path, label in selinux_labels.items():
                with suppress(ValueError):
                    selinux_label_file(resolve_selinux_path(str(path)), label)

            for port_attr in ( 'nsslapd-port', 'nsslapd-securePort' ):
                port = dse.get(DN_CONFIG, port_attr, single=True)
                if port is None or port == '0':
                    continue
                selinux_label_port(port)

    def dirsrv_instances(self, ignore_errors=False):
        inst_list = []
        for name in self.names:
            inst = DirSrv(verbose=DEBUGGING, external_log=log)
            try:
                inst.local_simple_allocate(name, binddn=DN_DM, password=PW_DM)
            except FileNotFoundError as ex:
                if ignore_errors is not True:
                    raise ex
                continue
            inst.setup_ldapi()
            inst_list.append(inst)
            nss = NssSsl(dirsrv=inst)
            nss.openssl_rehash(nss._certdb)
        return inst_list


    def extract_instances(self):
        with tarfile.open(name=TARFILENAME, mode='r:gz') as tar:
            tar.extractall(path=self.idir, filter=self.tar_filter)

        # Fix the dse.ldif
        for name in self.names:
            self.remap_dse(f'{self.idir}/etc/dirsrv/slapd-{name}/dse.ldif')

        # Create missing directories
        os.makedirs(f'{self.idir}/var/log/dirsrv/slapd-supplier1', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/var/log/dirsrv/slapd-supplier2', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/run/lock/dirsrv/slapd-supplier1', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/run/lock/dirsrv/slapd-supplier2', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/var/lib/dirsrv/slapd-supplier1/ldif', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/var/lib/dirsrv/slapd-supplier2/ldif', mode=0o750, exist_ok=True)
        os.makedirs(f'{self.idir}/run/dirsrv', mode=0o770, exist_ok=True)

        # Generate DirSrv instances
        self.inst_list = self.dirsrv_instances()

        # Relabel
        if self.uid == 0:
            self.relabel_selinux()


    def remove_instances(self):
        for inst in self.dirsrv_instances(ignore_errors=True):
            inst.delete()


    def migrate2mdb(self):
        args = FakeArgs()
        args.tmpdir = None
        for inst in self.inst_list:
            log.info(f'Migrating instance {inst.serverid} from bdb to mdb')
            dblib_bdb2mdb(inst, log, args)
            inst.start()
            inst.open()
        self.reset_agmt_passwords()


    def reset_agmt_passwords(self):
        pw = password_generate()

        for inst in self.inst_list:
            # For some reason inst.sslport is None although nsslapd-securePort is defined
            for service in ServiceAccounts(inst, DEFAULT_SUFFIX).list():
                service.replace('userPassword', pw)
            replica = Replicas(inst).list()[0]
            for agmt in replica.get_agreements().list():
                agmt.replace('nsds5ReplicaCredentials', pw)
        del pw


def strip_path(path):
    return re.match(r'^/*([^/].*)$', path).group(1)


@pytest.mark.skipif(get_default_db_lib() != "bdb", reason = f'Requires bdb mode')
@pytest.mark.skipif(os.getenv(GENTARBALL) is None, reason = f'Requires setting {GENTARBALL} environment variable')
def test_generate_tarball(topo_m2):
    """Test Generate the tarball for test_upgradefrombdb

    :id: 0325c4fe-da66-11ef-98fc-482ae39447e5
    :setup: two suppliers
    :steps:
        1. Check that replication is working
        2. Generaters the tarball
    :expectedresults:
        1. Success
        2. Success
    """
    idir = os.getenv("PREFIX", "/")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    for i1,i2 in permutations(topo_m2, 2):
        log.info(f'Testing replication {i1.serverid} --> {i2.serverid}')
        repl.test_replication(i1, i2)
    for inst in topo_m2:
        inst.stop()
    tardir = Path(TARFILENAME).parent
    os.makedirs(tardir, mode = 0o755, exist_ok = True)
    this_dir = os.getcwd()
    with tarfile.open(name=TARFILENAME, mode='w:gz') as tar:
        os.chdir(idir)
        for inst in topo_m2:
            name = inst.serverid
            tar.add(strip_path(f'/etc/dirsrv/slapd-{name}'))
            tar.add(strip_path(f'/var/lib/dirsrv/slapd-{name}/db'))
    os.chdir(this_dir)
    for inst in topo_m2:
        inst.start()


@pytest.mark.skipif(get_bdb_impl_status() != BDB_IMPL_STATUS.READ_ONLY, reason = 'Already tested through clu/dsctl_dblib_test.py:test_dblib_migration')
def test_upgradefrombdb():
    """Test upgrade from bdb to mdb

    :id: f0f02d12-da4f-11ef-966f-482ae39447e5
    :setup: None
    :steps:
        1. Extract bdb instances from the tar ball
        2. Migrate the instances to mdb
        3. Check that replication is still working
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    handler = MigrationHandler()
    handler.remove_instances()
    handler.extract_instances()
    handler.migrate2mdb()
    repl = ReplicationManager(DEFAULT_SUFFIX)
    for i1,i2 in permutations(handler.inst_list, 2):
        log.info(f'Testing replication {i1.serverid} --> {i2.serverid}')
        repl.test_replication(i1, i2)
    handler.remove_instances()

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    pytest.main(["-s", CURRENT_FILE])


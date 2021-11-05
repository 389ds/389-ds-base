# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import re
import glob
import shutil
from lib389.dseldif import DSEldif
import subprocess


DBLIB_LDIF_PREFIX = "__dblib-"
DEFAULT_DBMAP_SIZE = 2 * 1024 * 1024 * 1024
DBSIZE_MARGIN = 1.2
DBI_MARGIN = 60

MDB_INFO = "INFO.mdb"
MDB_MAP = "data.mdb"
MDB_LOCK = "lock.mdb"

LDBM_DN = "cn=config,cn=ldbm database,cn=plugins,cn=config"

_log = None


class FakeArgs(dict):
    # This calss is used for tests to generate an args
    # It allows to access a dict key as an attribute
    # (be cautious not to use a key that is a dict attibute like (keys, items, ... )
    def __init__(self, *args, **kwargs):
        super(FakeArgs, self).__init__(*args, **kwargs)
        self.__dict__ = self


def get_ldif_dir(instance):
    """
    Get the server's LDIF directory.
    """
    server_dir = instance.get_ldif_dir()
    if server_dir is not None:
        return server_dir
    return "/foo"


def get_backends(log, dse, tmpdir):
    """
    Get the backends and some associated data
    Note: inst is not connected so Backends(inst).list() cannot be used.
          So lets directly parse the data from the ldif file
    """
    res = {}
    for entry in dse._contents:
        found = re.search(r'^dn: cn=([^,]*), *cn=ldbm database.*', entry)
        if found:
            dn = found[0][4:]
            bename = found[1].lower()
            suffix = dse.get(dn, "nsslapd-suffix", True)
            dbdir = dse.get(dn, "nsslapd-directory", True)
            if dbdir is None and 'config' in res:
                dbdir = f"{res['config']['dbdir']}/{bename}"
            dblib = dse.get(dn, "nsslapd-backend-implement", True)
            ldifname = f'{tmpdir}/{DBLIB_LDIF_PREFIX}{bename}.ldif'
            cl5name = f'{tmpdir}/{DBLIB_LDIF_PREFIX}{bename}.cl5.dbtxt'
            cl5dbname = f'{dbdir}/replication_changelog.db'
            dbsize = 0
            entrysize = 0
            for f in glob.glob(f'{dbdir}/id2entry.db*'):
                entrysize = os.path.getsize(f)
            for f in glob.glob(f'{dbdir}/*.db*'):
                dbsize += os.path.getsize(f)
            indexes = dse.get_indexes(bename)
            # Let estimate the number of dbis: id2entry + 1 per regular index + 2 per vlv index
            dbi = 1 + len(indexes) + len([index for index in indexes if index.startswith("vlv#")])

            res[bename] = ({
                'dn': dn,
                'bename': bename,
                'suffix': suffix,
                'dbdir': dbdir,
                'dbsize': dbsize,
                'dblib': dblib,
                'ldifname': ldifname,
                'cl5name': cl5name,
                'cl5dbname': cl5dbname,
                'dbsize': dbsize,
                'entrysize': entrysize,
                'indexes': indexes,
                'dbi': dbi
            })
    log.debug(f'lib389.cli_ctl.dblib.get_backends returns: {str(res)}')
    return res


def get_mdb_dbis(dbdir):
    # Returns a map  containings associating the backend (Could be None)
    # to a map associating the dbi filename to a map describing its statistics.
    result = {}
    output = run_dbscan(['-D', 'mdb', '-L', dbdir])
    for line in output.split("\n"):
        found = re.search(f'^ {dbdir}/?([^/]*)?/([^/]*) flags: .*state:.([^ ]*).*nb_entries=([0-9]*)', line)
        if (found):
            bename = found[1].lower()
            if bename == '':
                bename = None
            filename = found[2]
            state = found[3]
            nbentries = found[4]
            if bename not in result:
                result[bename] = {}
            result[bename][filename] = {'bename': bename, 'filename': filename, 'state': state, 'nbentries': nbentries}
    return result


def size_fmt(num):
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if (num < 1024):
            return f"{num:3.1f} {unit}"
        num /= 1024
    return f"{num:.1f} TB"


def run_dbscan(args):
    prefix = os.environ.get('PREFIX', "")
    prog = f'{prefix}/bin/dbscan'
    args.insert(0, prog)
    try:
        output = subprocess.check_output(args, encoding='utf-8', stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        _log.error(f"run_dbscan failed: cmd is {e.cmd} return code is {e.returncode} output is {str(e.output)}")
        _log.exception(e)
        raise e
    return output


def export_changelog(be, dblib):
    # Export backend changelog
    try:
        run_dbscan(['-D', dblib, '-f', be['cl5dbname'], '-X', be['cl5name']])
        return True
    except subprocess.CalledProcessError as e:
        return False


def import_changelog(be, dblib):
    # import backend changelog
    try:
        run_dbscan(['-D', dblib, '-f', be['cl5dbname'], '-I', be['cl5name']])
        return True
    except subprocess.CalledProcessError as e:
        return False


def dblib_bdb2mdb(inst, log, args):
    global _log
    _log = log
    if args.tmpdir is None:
        tmpdir = get_ldif_dir(inst)
    else:
        tmpdir = args.tmpdir
    try:
        os.makedirs(tmpdir, 0o750, True)
    except OSError as e:
        log.error(f"Failed trying to create the directory {tmpdir} needed to store the ldif files, error: {str(e)}")
        return

    # Cannot use Backends(inst).list() because it requires a connection.
    # lets use directlt the dse.ldif after having stopped the instance

    inst.stop()
    dse = DSEldif(inst)
    backends = get_backends(log, dse, tmpdir)
    dbmapdir = backends['config']['dbdir']
    dblib = backends['config']['dblib']

    if dblib == "mdb":
        log.error(f"Instance {inst.serverid} is already configured with lmdb.")
        return

    # Remove ldif files and mdb files
    dblib_cleanup(inst, log, args)

    # Compute the needed space and the lmdb map configuration
    total_dbsize = 0
    total_entrysize = 0
    total_dbi = 3
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if be['dbsize'] == 0:
            continue
        total_dbsize += be['dbsize']
        total_entrysize += be['entrysize']
        total_dbi += be['dbi']

    # Round up dbmap size
    dbmap_size = DEFAULT_DBMAP_SIZE
    while (total_dbsize * DBSIZE_MARGIN > dbmap_size):
        dbmap_size *= 1.25

    # Round up number of dbis
    nbdbis = 1
    while nbdbis < total_dbi + DBI_MARGIN:
        nbdbis *= 2

    log.info(f"Required space for LDIF files is about {size_fmt(total_entrysize)}")
    log.info(f"Required space for DBMAP files is about {size_fmt(dbmap_size)}")
    log.info(f"Required number of dbi is {nbdbis}")

    # Generate the info file (so dbscan could generate the map)
    uid = inst.get_user_uid()
    gid = inst.get_group_gid()
    with open(f'{dbmapdir}/{MDB_INFO}', 'w') as f:
        f.write('LIBVERSION=9025\n')
        f.write('DATAVERSION=0\n')
        f.write(f'MAXSIZE={dbmap_size}\n')
        f.write('MAXREADERS=50\n')
        f.write(f'MAXDBS={nbdbis}\n')
    os.chown(f'{dbmapdir}/{MDB_INFO}', uid, gid)

    if os.stat(dbmapdir).st_dev == os.stat(tmpdir).st_dev:
        total, used, free = shutil.disk_usage(dbmapdir)
        if free < total_entrysize + dbmap_size:
            log.error(f"Not enough space on {dbmapdir} to migrate to lmdb (Need {size_fmt(total_entrysize + dbmap_size)}, Have {size_fmt(free)})")
            return
    else:
        total, used, free = shutil.disk_usage(dbmapdir)
        if free < dbmap_size:
            log.error(f"Not enough space on {dbmapdir} to migrate to lmdb (Need {size_fmt(dbmap_size)}, Have {size_fmt(free)})")
            return
        total, used, free = shutil.disk_usage(tmpdir)
        if free < total_entrysize:
            log.error("Not enough space on {tmpdir} to migrate to lmdb (Need {size_fmt(total_entrysize)}, Have {size_fmt(free)})")
            return
    progress = 0
    encrypt = False       # Should maybe be a args param
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if be['dbsize'] == 0:
            continue
        log.info(f"Backends exportation {progress*100/total_dbsize:2f}% ({bename})")
        log.debug(f"inst.db2ldif({bename}, None, None, {encrypt}, True, {be['ldifname']})")
        inst.db2ldif(bename, None, None, encrypt, True, be['ldifname'], False)
        be['cl5'] = export_changelog(be, 'bdb')
        progress += be['dbsize']
    log.info("Backends exportation 100%")

    log.info("Updating dse.ldif file")
    # switch nsslapd-backend-implement in the dse.ldif
    cfgbe = backends['config']
    dn = cfgbe['dn']
    dse.replace(dn, 'nsslapd-backend-implement', 'mdb')

    # Add the lmdb config entry
    dn = f'cn=mdb,{dn}'
    try:
        dse.delete_dn(dn)
    except Exception:
        pass
    dse.add_entry([
        f"dn: {dn}\n",
        "objectClass: extensibleobject\n",
        "objectClass: top\n",
        "cn: mdb\n",
        f"nsslapd-mdb-max-size: {dbmap_size}\n",
        "nsslapd-mdb-max-readers: 0\n",
        f"nsslapd-mdb-max-dbs: {nbdbis}\n",
        "nsslapd-db-durable-transaction: on\n",
        "nsslapd-search-bypass-filter-test: on\n",
        "nsslapd-serial-lock: on\n"
    ])

    # Reimport all exported backends and changelog
    progress = 0
    encrypt = False       # Should maybe be a args param
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if be['dbsize'] == 0:
            continue
        log.info(f"Backends importation {progress*100/total_dbsize:2f}% ({bename})")
        os.chown(be['ldifname'], uid, gid)
        log.debug(f"inst.ldif2db({bename}, None, None, {encrypt}, {be['ldifname']})")
        inst.ldif2db(bename, None, None, encrypt, be['ldifname'])
        if be['cl5'] is True:
            import_changelog(be, 'mdb')
        progress += be['dbsize']
    dbhome=backends["config"]["dbdir"]
    for f in glob.glob(f'{dbhome}/*.mdb'):
        os.chown(f, uid, gid)
    log.info("Backends importation 100%")
    inst.start()
    log.info("Migration from Berkeley database to lmdb is done.")


def dblib_mdb2bdb(inst, log, args):
    global _log
    _log = log
    if args.tmpdir is None:
        tmpdir = get_ldif_dir(inst)
    else:
        tmpdir = args.tmpdir
    try:
        os.makedirs(tmpdir, 0o750, True)
    except OSError as e:
        log.error(f"Failed trying to create the directory {tmpdir} needed to store the ldif files, error: {str(e)}")
        return

    # Cannot use Backends(inst).list() because it requires a connection.
    # lets use directlt the dse.ldif after having stopped the instance

    inst.stop()
    dse = DSEldif(inst)
    backends = get_backends(log, dse, tmpdir)
    dbmapdir = backends['config']['dbdir']
    dblib = backends['config']['dblib']
    dbis = get_mdb_dbis(dbmapdir)

    if dblib == "bdb":
        log.error(f"Instance {inst.serverid} is already configured with bdb.")
        return

    # Remove ldif files and bdb files
    dblib_cleanup(inst, log, args)

    for be in dbis:
        if be is None:
            continue
        id2entry = dbis[be]['id2entry.db']
        if int(id2entry['nbentries']) > 0:
            backends[be]['has_id2entry'] = True

    # Compute the needed space and the lmdb map configuration
    dbmap_size = os.path.getsize(f'{dbmapdir}/{MDB_MAP}')
    # Clearly over evaluated (but better than nothing )
    total_entrysize = dbmap_size

    log.info(f"Required space for LDIF files is about {size_fmt(total_entrysize)}")
    log.info(f"Required space for bdb files is about {size_fmt(dbmap_size)}")

    if os.stat(dbmapdir).st_dev == os.stat(tmpdir).st_dev:
        total, used, free = shutil.disk_usage(dbmapdir)
        if free < total_entrysize + dbmap_size:
            log.error(f"Not enough space on {dbmapdir} to migrate to bdb (Need {size_fmt(total_entrysize + dbmap_size)}, Have {size_fmt(free)})")
            return
    else:
        total, used, free = shutil.disk_usage(dbmapdir)
        if free < dbmap_size:
            log.error(f"Not enough space on {dbmapdir} to migrate to bdb (Need {size_fmt(dbmap_size)}, Have {size_fmt(free)})")
            return
        total, used, free = shutil.disk_usage(tmpdir)
        if free < total_entrysize:
            log.error("Not enough space on {tmpdir} to migrate to bdb (Need {size_fmt(total_entrysize)}, Have {size_fmt(free)})")
            return
    progress = 0
    encrypt = False       # Should maybe be a args param
    total_dbsize = 0
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if 'has_id2entry' not in be:
            continue
        total_dbsize += 1
    uid = inst.get_user_uid()
    gid = inst.get_group_gid()
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if 'has_id2entry' not in be:
            continue
        log.info(f"Backends exportation {progress*100/total_dbsize:2f}% ({bename})")
        log.debug(f"inst.db2ldif({bename}, None, None, {encrypt}, True, {be['ldifname']})")
        inst.db2ldif(bename, None, None, encrypt, True, be['ldifname'], False)
        be['cl5'] = export_changelog(be, 'mdb')
        progress += 1
    log.info("Backends exportation 100%")
    dbhome=backends["config"]["dbdir"]
    for f in glob.glob(f'{dbhome}/*'):
        os.chown(f, uid, gid)

    log.info("Updating dse.ldif file")
    # switch nsslapd-backend-implement in the dse.ldif
    cfgbe = backends['config']
    dn = cfgbe['dn']
    dse.replace(dn, 'nsslapd-backend-implement', 'bdb')

    # bdb entries should still be here

    # Reimport all exported backends and changelog
    progress = 0
    encrypt = False      # Should maybe be a args param
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if 'has_id2entry' not in be:
            continue
        log.info(f"Backends importation {progress*100/total_dbsize:2f}% ({bename})")
        log.debug(f"inst.ldif2db({bename}, None, None, {encrypt}, {be['ldifname']})")
        os.chown(be['ldifname'], uid, gid)
        inst.ldif2db(bename, None, None, encrypt, be['ldifname'])
        if be['cl5'] is True:
            import_changelog(be, 'bdb')
        for f in glob.glob(f'{be["dbdir"]}/*'):
            os.chown(f, uid, gid)
        progress += be['dbsize']
    log.info("Backends importation 100%")
    inst.start()
    log.info("Migration from ldbm to Berkeley database is done.")


def rm(path):
    if path is not None:
        try:
            os.remove(path)
        except FileNotFoundError:
            pass


def dblib_cleanup(inst, log, args):
    global _log
    _log = log
    tmpdir = get_ldif_dir(inst)
    dse = DSEldif(inst)
    backends = get_backends(log, dse, tmpdir)
    dbmapdir = backends['config']['dbdir']
    dbhome = inst.ds_paths.db_home_dir
    dblib = backends['config']['dblib']

    # Remove all ldif and changelog file
    for bename, be in backends.items():
        # Keep only backend associated with a db
        if 'has_id2entry' not in be and be['dbsize'] == 0:
            continue
        rm(be['ldifname'])
        rm(be['cl5name'])
        dbdir = be['dbdir']
        if dblib == "mdb":
            # remove db dir
            for f in glob.glob(f'{dbdir}/*.db*'):
                rm(f)
            rm(f'{dbdir}/DBVERSION')
            os.rmdir(dbdir)

    if dblib == "bdb":
        rm(f'{dbmapdir}/INFO.mdb')
        rm(f'{dbmapdir}/data.mdb')
        rm(f'{dbmapdir}/lock.mdb')
    else:
        for f in glob.glob(f'{dbhome}/__db.*'):
            rm(f)
        for f in glob.glob(f'{dbmapdir}/log.*'):
            rm(f)
        rm(f'{dbhome}/DBVERSION')
        rm(f'{dbmapdir}/guardian')


def create_parser(subparsers):
    dblib_parser = subparsers.add_parser('dblib', help="database library (i.e bdb/lmdb) migration")
    subcommands = dblib_parser.add_subparsers(help="action")

    dblib_bdb2mdb_parser = subcommands.add_parser('bdb2mdb', help='Migrate bdb databases to lmdb')
    dblib_bdb2mdb_parser.set_defaults(func=dblib_bdb2mdb)
    dblib_bdb2mdb_parser.add_argument('--tmpdir', help="ldif migration files directory path.")

    dblib_mdb2bdb_parser = subcommands.add_parser('mdb2bdb', help='Migrate lmdb databases to bdb')
    dblib_mdb2bdb_parser.set_defaults(func=dblib_mdb2bdb)
    dblib_mdb2bdb_parser.add_argument('--tmpdir', help="ldif migration files directory path.")

    dblib_cleanup_parser = subcommands.add_parser('cleanup', help='Remove migration ldif file and old database')
    dblib_cleanup_parser.set_defaults(func=dblib_cleanup)

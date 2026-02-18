# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import glob
import os
import re
import signal
import subprocess
from enum import Enum
from lib389._constants import TaskWarning
from lib389.cli_base import CustomHelpFormatter
from lib389.dseldif import DSEldif
from pathlib import Path


class IndexOrdering(Enum):
    """Represents the ordering type of an index."""
    INTEGER = "integer"
    LEXICOGRAPHIC = "lexicographic"
    UNKNOWN = "unknown"


def dbtasks_db2index(inst, log, args):
    rtn = False
    if not args.backend:
        if not inst.db2index():
            rtn = False
        else:
            rtn = True
    elif args.backend and not args.attr:
        if not inst.db2index(bename=args.backend):
            rtn = False
        else:
            rtn = True
    else:
        if not inst.db2index(bename=args.backend, attrs=args.attr):
            rtn = False
        else:
            rtn = True
    if rtn:
        log.info("db2index successful")
        return rtn
    else:
        log.fatal("db2index failed")
        return rtn


def dbtasks_db2bak(inst, log, args):
    # Needs an output name?
    if not inst.db2bak(args.archive):
        log.fatal("db2bak failed")
        return False
    else:
        log.info("db2bak successful")


def dbtasks_bak2db(inst, log, args):
    # Needs the archive to restore.
    if not inst.bak2db(args.archive):
        log.fatal("bak2db failed")
        return False
    else:
        log.info("bak2db successful")


def dbtasks_db2ldif(inst, log, args):
    # If export filename is provided, check if file path exists
    if args.ldif:
        path = Path(args.ldif)
        parent = path.parent.absolute()
        if not parent.exists():
            raise ValueError("The LDIF export location does not exist: "
                             + args.ldif)

    # Export backend
    if not inst.db2ldif(bename=args.backend, encrypt=args.encrypted, repl_data=args.replication,
                        outputfile=args.ldif, suffixes=None, excludeSuffixes=None, export_cl=False):
        log.fatal("db2ldif failed")
        return False
    else:
        log.info("db2ldif successful")


def dbtasks_ldif2db(inst, log, args):
    # Check if ldif file exists
    if not os.path.exists(args.ldif):
        raise ValueError("The LDIF file does not exist: " + args.ldif)

    ret = inst.ldif2db(bename=args.backend, encrypt=args.encrypted, import_file=args.ldif,
                        suffixes=None, excludeSuffixes=None, import_cl=False)
    if not ret:
        log.fatal("ldif2db failed")
        return False
    elif ret == TaskWarning.WARN_SKIPPED_IMPORT_ENTRY:
        log.warn("ldif2db successful with skipped entries")
    else:
        log.info("ldif2db successful")


def dbtasks_backups(inst, log, args):
    if args.delete:
        # Delete backup
        inst.del_backup(args.delete[0])
    else:
        # list backups
        if not inst.backups(args.json):
            log.fatal("Failed to get list of backups")
            return False
        else:
            if args.json is None:
                log.info("backups successful")


def dbtasks_ldifs(inst, log, args):
    if args.delete:
        # Delete LDIF file
        inst.del_ldif(args.delete[0])
    else:
        # list LDIF files
        if not inst.ldifs(args.json):
            log.fatal("Failed to get list of LDIF files")
            return False
        else:
            if args.json is None:
                log.info("backups successful")


def dbtasks_verify(inst, log, args):
    if not inst.dbverify(bename=args.backend):
        log.fatal("dbverify failed")
        return False
    else:
        log.info("dbverify successful")


def _get_db_dir(dse_ldif):
    """Get the database directory.

    Args:
        dse_ldif: DSEldif instance.

    Returns:
        Path to the database directory, or None if not found.
    """
    try:
        db_dir = dse_ldif.get(
            "cn=config,cn=ldbm database,cn=plugins,cn=config",
            "nsslapd-directory",
            single=True,
        )
        return db_dir
    except (ValueError, TypeError):
        pass
    return None



def _has_integer_ordering_match(dse_ldif, backend, index_name):
    """Check if an index has integerOrderingMatch configured in DSE.

    Args:
        dse_ldif: DSEldif instance.
        backend: Backend name.
        index_name: Name of the index to check.

    Returns:
        True if integerOrderingMatch is configured, False otherwise.
    """
    index_dn = "cn={},cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(
        index_name, backend
    )
    matching_rules = dse_ldif.get(index_dn, "nsMatchingRule", lower=True)
    if matching_rules:
        return any(mr.lower() == "integerorderingmatch" for mr in matching_rules)
    return False


def _has_index_scan_limit(dse_ldif, backend, index_name):
    """Check if an index has nsIndexIDListScanLimit configured.

    Args:
        dse_ldif: DSEldif instance.
        backend: Backend name.
        index_name: Name of the index to check.

    Returns:
        True if nsIndexIDListScanLimit is configured, False otherwise.
    """
    index_dn = "cn={},cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(
        index_name, backend
    )
    scan_limit = dse_ldif.get(index_dn, "nsIndexIDListScanLimit")
    return scan_limit is not None


def _index_config_exists(dse_ldif, backend, index_name):
    """Check if an index configuration entry exists in DSE.

    Args:
        dse_ldif: DSEldif instance.
        backend: Backend name.
        index_name: Name of the index to check.

    Returns:
        True if the index config entry exists, False otherwise.
    """
    index_dn = "cn={},cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(
        index_name, backend
    )
    try:
        cn = dse_ldif.get(index_dn, "cn")
        return cn is not None
    except (ValueError, KeyError):
        return False


def _default_index_exists(dse_ldif, index_name):
    """Check if an index exists in cn=default indexes.

    Args:
        dse_ldif: DSEldif instance.
        index_name: Name of the index to check.

    Returns:
        True if the index exists in default indexes, False otherwise.
    """
    index_dn = "cn={},cn=default indexes,cn=config,cn=ldbm database,cn=plugins,cn=config".format(
        index_name
    )
    try:
        cn = dse_ldif.get(index_dn, "cn")
        return cn is not None
    except (ValueError, KeyError):
        return False


def _check_disk_ordering(db_dir, backend, index_name, dbscan_path, is_mdb, log):
    """Check if index on disk uses lexicographic or integer ordering.

    Args:
        db_dir: Path to the database directory.
        backend: Backend name.
        index_name: Name of the index to check.
        dbscan_path: Path to the dbscan binary.
        is_mdb: True if using MDB backend.
        log: Logger instance.

    Returns:
        IndexOrdering: The detected ordering type.
    """
    if is_mdb:
        # MDB uses pseudo-paths: db_dir/backend/index.db
        # dbscan accesses indexes via paths like: /var/lib/dirsrv/slapd-xxx/db/userroot/parentid.db
        index_file = os.path.join(db_dir, backend, "{}.db".format(index_name))
    else:
        # BDB has separate directories per backend with actual index files
        backend_dir = os.path.join(db_dir, backend)
        if not os.path.exists(backend_dir):
            return IndexOrdering.UNKNOWN
        index_file = None
        pattern = os.path.join(backend_dir, "{}.db*".format(index_name))
        for f in glob.glob(pattern):
            if os.path.isfile(f):
                index_file = f
                break
        if not index_file:
            return IndexOrdering.UNKNOWN

    # Only read the first 100 lines from dbscan to avoid scanning the
    # entire index (which can take hours on large databases).
    try:
        proc = subprocess.Popen(
            [dbscan_path, "-f", index_file],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )

        keys = []
        line_count = 0
        assert proc.stdout is not None
        for line in proc.stdout:
            line_count += 1
            if line_count > 100:
                break
            line = line.strip()
            if line.startswith("="):
                match = re.match(r"^=(\d+)", line)
                if match:
                    keys.append(int(match.group(1)))

        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

        if proc.returncode not in (0, -signal.SIGTERM):
            log.warning("  dbscan returned non-zero exit code for %s", index_file)
            return IndexOrdering.UNKNOWN

        if len(keys) < 2:
            return IndexOrdering.UNKNOWN

        # Check if keys are in integer order by looking for decreasing numeric values
        # (which would indicate lexicographic ordering, e.g., "3" < "30" < "4")
        prev_id = keys[0]
        for current_id in keys[1:]:
            if prev_id > current_id:
                return IndexOrdering.LEXICOGRAPHIC
            prev_id = current_id

        return IndexOrdering.INTEGER

    except OSError as e:
        log.warning("  Error running dbscan: %s", e)
        return IndexOrdering.UNKNOWN


def dbtasks_index_check(inst, log, args):
    """Check and optionally fix index ordering mismatches.

    This function detects mismatches between the configured ordering
    (integerOrderingMatch in DSE) and the actual on-disk ordering of
    parentid and ancestorid indexes.

    Args:
        inst: DirSrv instance.
        log: Logger instance.
        args: Parsed command line arguments.

    Returns:
        True if all checks passed, False if mismatches were detected.
    """
    # Server must be stopped
    if inst.status():
        log.error("index-check requires the instance to be stopped")
        return False

    # Check for dbscan binary
    dbscan_path = os.path.join(inst.ds_paths.bin_dir, "dbscan")
    if not os.path.exists(dbscan_path):
        log.error("dbscan utility not found at %s", dbscan_path)
        return False

    # Load DSE
    try:
        dse_ldif = DSEldif(inst)
    except Exception as e:
        log.error("Failed to read dse.ldif: %s", e)
        return False

    # Get backends to check
    all_backends = dse_ldif.get_backends()
    if not all_backends:
        log.info("No backends found")
        return True

    # Filter to specific backend if requested
    if args.backend:
        # Case-insensitive backend lookup
        backend_lower = args.backend.lower()
        matching_backend = None
        for be in all_backends:
            if be.lower() == backend_lower:
                matching_backend = be
                break
        if matching_backend is None:
            log.error("Backend '%s' not found. Available backends: %s",
                      args.backend, ", ".join(all_backends))
            return False
        backends_to_check = [matching_backend]
    else:
        backends_to_check = all_backends

    # Get database directory and check database type
    db_dir = _get_db_dir(dse_ldif)
    if not db_dir or not os.path.exists(db_dir):
        log.error("Database directory not found")
        return False

    db_lib = inst.get_db_lib()
    is_mdb = (db_lib == "mdb")
    log.info("Database type: %s", db_lib.upper())

    # Track all issues found
    all_ok = True
    config_fixes = []  # (backend, index_name, action) tuples: action is "add_mr" or "remove_mr"
    scan_limits_to_remove = []  # (backend, index_name) tuples with nsIndexIDListScanLimit
    ancestorid_configs_to_remove = []  # backend names with ancestorid config entries
    remove_ancestorid_from_defaults = False  # Flag to remove from cn=default indexes

    # Check if ancestorid exists in cn=default indexes (should be removed)
    if _default_index_exists(dse_ldif, "ancestorid"):
        log.warning("ancestorid found in cn=default indexes - should be removed")
        remove_ancestorid_from_defaults = True
        all_ok = False

    for backend in backends_to_check:
        log.info("Checking backend: %s", backend)

        # Check for ancestorid config entry (should not exist)
        if _index_config_exists(dse_ldif, backend, "ancestorid"):
            log.warning("  ancestorid - config entry exists (should be removed)")
            ancestorid_configs_to_remove.append(backend)
            all_ok = False

        # Check parentid and ancestorid indexes
        for index_name in ["parentid", "ancestorid"]:
            # Check for scan limits (should be removed)
            if _has_index_scan_limit(dse_ldif, backend, index_name):
                log.warning("  %s - has nsIndexIDListScanLimit (should be removed)", index_name)
                scan_limits_to_remove.append((backend, index_name))
                all_ok = False

            # Check disk ordering
            disk_ordering = _check_disk_ordering(db_dir, backend, index_name, dbscan_path, is_mdb, log)

            if disk_ordering == IndexOrdering.UNKNOWN:
                log.info("  %s - could not determine disk ordering, skipping", index_name)
                continue

            config_has_int_order = _has_integer_ordering_match(dse_ldif, backend, index_name)
            config_desc = "integer" if config_has_int_order else "lexicographic"
            log.info("  %s - config: %s, disk: %s",
                     index_name, config_desc, disk_ordering.value)

            # Both orderings are valid for parentid, but config must match disk.
            if index_name == "parentid":
                if config_has_int_order and disk_ordering == IndexOrdering.LEXICOGRAPHIC:
                    log.warning("  %s - MISMATCH: config has integerOrderingMatch but disk is lexicographic", index_name)
                    config_fixes.append((backend, index_name, "remove_mr"))
                    all_ok = False
                elif not config_has_int_order and disk_ordering == IndexOrdering.INTEGER:
                    log.warning("  %s - MISMATCH: config is lexicographic but disk has integer ordering", index_name)
                    config_fixes.append((backend, index_name, "add_mr"))
                    all_ok = False

    # Handle issues
    if not all_ok:
        if args.fix:
            log.info("Fixing issues...")

            # Remove ancestorid from cn=default indexes
            if remove_ancestorid_from_defaults:
                default_idx_dn = "cn=ancestorid,cn=default indexes,cn=config,cn=ldbm database,cn=plugins,cn=config"
                log.info("  Removing ancestorid from default indexes...")
                try:
                    dse_ldif.delete_dn(default_idx_dn)
                    log.info("  Removed ancestorid from default indexes")
                except Exception as e:
                    log.error("  Failed to remove ancestorid from default indexes: %s", e)
                    return False

            # Remove scan limits (only for indexes that won't be deleted)
            for backend, index_name in scan_limits_to_remove:
                # Skip ancestorid if we're going to delete the whole entry anyway
                if index_name == "ancestorid" and backend in ancestorid_configs_to_remove:
                    continue
                index_dn = "cn={},cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(
                    index_name, backend
                )
                log.info("  Removing nsIndexIDListScanLimit from %s in backend %s...", index_name, backend)
                try:
                    dse_ldif.delete(index_dn, "nsIndexIDListScanLimit")
                    log.info("  Removed nsIndexIDListScanLimit from %s", index_name)
                except Exception as e:
                    log.error("  Failed to remove nsIndexIDListScanLimit from %s: %s", index_name, e)
                    return False

            # Remove ancestorid config entries from backends
            for backend in ancestorid_configs_to_remove:
                index_dn = "cn=ancestorid,cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(backend)
                log.info("  Removing ancestorid config entry from backend %s...", backend)
                try:
                    dse_ldif.delete_dn(index_dn)
                    log.info("  Removed ancestorid config entry from backend %s", backend)
                except Exception as e:
                    log.error("  Failed to remove ancestorid config from backend %s: %s", backend, e)
                    return False

            # Fix config-vs-disk ordering mismatches by adjusting config to match disk
            for backend, index_name, action in config_fixes:
                index_dn = "cn={},cn=index,cn={},cn=ldbm database,cn=plugins,cn=config".format(
                    index_name, backend
                )
                if action == "add_mr":
                    log.info("  Adding integerOrderingMatch to %s in backend %s...", index_name, backend)
                    try:
                        dse_ldif.add(index_dn, "nsMatchingRule", "integerOrderingMatch")
                        log.info("  Updated dse.ldif with integerOrderingMatch for %s", index_name)
                    except Exception as e:
                        log.error("  Failed to update dse.ldif for %s: %s", index_name, e)
                        return False
                elif action == "remove_mr":
                    log.info("  Removing integerOrderingMatch from %s in backend %s...", index_name, backend)
                    try:
                        dse_ldif.delete(index_dn, "nsMatchingRule", "integerOrderingMatch")
                        log.info("  Removed integerOrderingMatch from %s", index_name)
                    except Exception as e:
                        log.error("  Failed to remove integerOrderingMatch from %s: %s", index_name, e)
                        return False

            log.info("All issues fixed")
            return True
        else:
            log.info("Issues detected. Run with --fix to repair.")
            return False
    else:
        log.info("All checks passed - no issues found")
        return True


def create_parser(subcommands):
    db2index_parser = subcommands.add_parser('db2index', help="Initialise a reindex of the server database. The server must be stopped for this to proceed.", formatter_class=CustomHelpFormatter)
    # db2index_parser.add_argument('suffix', help="The suffix to reindex. IE dc=example,dc=com.")
    db2index_parser.add_argument('backend', nargs="?", help="The backend to reindex. IE userRoot", default=False)
    db2index_parser.add_argument('--attr', nargs="*", help="The attribute's to reindex. IE --attr aci cn givenname", default=False)
    db2index_parser.set_defaults(func=dbtasks_db2index)

    db2bak_parser = subcommands.add_parser('db2bak', help="Initialise a BDB backup of the database. The server must be stopped for this to proceed.", formatter_class=CustomHelpFormatter)
    db2bak_parser.add_argument('archive', help="The destination for the archive. This will be created during the db2bak process.",
                               nargs='?', default=None)
    db2bak_parser.set_defaults(func=dbtasks_db2bak)

    db2ldif_parser = subcommands.add_parser('db2ldif', help="Initialise an LDIF dump of the database. The server must be stopped for this to proceed.", formatter_class=CustomHelpFormatter)
    db2ldif_parser.add_argument('backend', help="The backend to output as an LDIF. IE userRoot")
    db2ldif_parser.add_argument('ldif', help="The path to the ldif output location.", nargs='?', default=None)
    db2ldif_parser.add_argument('--replication', help="Export replication information, suitable for importing on a new consumer or backups.",
                                default=False, action='store_true')
    # db2ldif_parser.add_argument('--include-changelog', help="Include the changelog as a separate LDIF file which will be named:  <ldif_file_name>_cl.ldif.  "
    #                                                         "This option also implies the '--replication' option is set.",
    #                             default=False, action='store_true')
    db2ldif_parser.add_argument('--encrypted', help="Export encrypted attributes", default=False, action='store_true')
    db2ldif_parser.set_defaults(func=dbtasks_db2ldif)

    dbverify_parser = subcommands.add_parser('dbverify', help="Perform a db verification. You should only do this at direction of support", formatter_class=CustomHelpFormatter)
    dbverify_parser.add_argument('backend', help="The backend to verify. IE userRoot")
    dbverify_parser.set_defaults(func=dbtasks_verify)

    bak2db_parser = subcommands.add_parser('bak2db', help="Restore a BDB backup of the database. The server must be stopped for this to proceed.", formatter_class=CustomHelpFormatter)
    bak2db_parser.add_argument('archive', help="The archive to restore. This will erase all current server databases.")
    bak2db_parser.set_defaults(func=dbtasks_bak2db)

    ldif2db_parser = subcommands.add_parser('ldif2db', help="Restore an LDIF dump of the database. The server must be stopped for this to proceed.", formatter_class=CustomHelpFormatter)
    ldif2db_parser.add_argument('backend', help="The backend to restore from an LDIF. IE userRoot")
    ldif2db_parser.add_argument('ldif', help="The path to the ldif to import")
    ldif2db_parser.add_argument('--encrypted', help="Import encrypted attributes", default=False, action='store_true')
    # ldif2db_parser.add_argument('--include-changelog', help="Include a replication changelog LDIF file if present.  It must be named like this in order for the import to include it:  <ldif_file_name>_cl.ldif.",
    #                            default=False, action='store_true')
    ldif2db_parser.set_defaults(func=dbtasks_ldif2db)

    backups_parser = subcommands.add_parser('backups', help="List backup's found in the server's default backup directory", formatter_class=CustomHelpFormatter)
    backups_parser.add_argument('--delete', nargs=1, help="Delete backup directory")
    backups_parser.set_defaults(func=dbtasks_backups)

    ldifs_parser = subcommands.add_parser('ldifs', help="List all the LDIF files located in the server's LDIF directory", formatter_class=CustomHelpFormatter)
    ldifs_parser.add_argument('--delete', nargs=1, help="Delete LDIF file")
    ldifs_parser.set_defaults(func=dbtasks_ldifs)

    index_check_parser = subcommands.add_parser('index-check',
        help="Check for index ordering mismatches (parentid/ancestorid). The server must be stopped.",
        formatter_class=CustomHelpFormatter)
    index_check_parser.add_argument('backend', nargs='?', default=None,
        help="Backend to check. If not specified, all backends are checked.")
    index_check_parser.add_argument('--fix', action='store_true', default=False,
        help="Fix mismatches by adjusting config to match on-disk data")
    index_check_parser.set_defaults(func=dbtasks_index_check)

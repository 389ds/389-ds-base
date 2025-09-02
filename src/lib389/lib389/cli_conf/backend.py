# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.backend import Backend, Backends, DatabaseConfig, BackendSuffixView
from lib389.configurations.sample import (
    create_base_domain,
    create_base_org,
    create_base_orgunit,
    create_base_cn,
    create_base_c,
    )
from lib389.chaining import (ChainingLinks)
from lib389.monitor import MonitorLDBM
from lib389.replica import Replicas
from lib389.utils import ensure_str, is_a_dn, is_dn_parent
from lib389.tasks import DBCompactTask
from lib389._constants import INSTALL_LATEST_CONFIG
from lib389.properties import BACKEND_SAMPLE_ENTRIES
from lib389.cli_base import (
    _format_status,
    _generic_get,
    _generic_get_dn,
    _get_arg,
    _warn,
    CustomHelpFormatter
    )
import json
import ldap
from ldap.dn import str2dn

arg_to_attr = {
        'lookthroughlimit': 'nsslapd-lookthroughlimit',
        'mode': 'nsslapd-mode',
        'state': 'nsslapd-state',
        'idlistscanlimit': 'nsslapd-idlistscanlimit',
        'directory': 'nsslapd-directory',
        'dbcachesize': 'nsslapd-dbcachesize',
        'logdirectory': 'nsslapd-db-logdirectory',
        'txn_wait': 'nsslapd-db-transaction-wait',
        'checkpoint_interval': 'nsslapd-db-checkpoint-interval',
        'compactdb_interval': 'nsslapd-db-compactdb-interval',
        'compactdb_time': 'nsslapd-db-compactdb-time',
        'txn_batch_val': 'nsslapd-db-transaction-batch-val',
        'txn_batch_min': 'nsslapd-db-transaction-batch-min-wait',
        'txn_batch_max': 'nsslapd-db-transaction-batch-max-wait',
        'logbufsize': 'nsslapd-db-logbuf-size',
        'locks': 'nsslapd-db-locks',
        'locks_monitoring_enabled': 'nsslapd-db-locks-monitoring-enabled',
        'locks_monitoring_threshold': 'nsslapd-db-locks-monitoring-threshold',
        'locks_monitoring_pause': 'nsslapd-db-locks-monitoring-pause',
        'import_cache_autosize': 'nsslapd-import-cache-autosize',
        'cache_autosize': 'nsslapd-cache-autosize',
        'cache_autosize_split': 'nsslapd-cache-autosize-split',
        'import_cachesize': 'nsslapd-import-cachesize',
        'exclude_from_export': 'nsslapd-exclude-from-export',
        'pagedlookthroughlimit': 'nsslapd-pagedlookthroughlimit',
        'pagedidlistscanlimit': 'nsslapd-pagedidlistscanlimit',
        'rangelookthroughlimit': 'nsslapd-rangelookthroughlimit',
        'backend_opt_level': 'nsslapd-backend-opt-level',
        'deadlock_policy': 'nsslapd-db-deadlock-policy',
        'db_home_directory': 'nsslapd-db-home-directory',
        'db_lib': 'nsslapd-backend-implement',
        'mdb_max_size': 'nsslapd-mdb-max-size',
        'mdb_max_readers': 'nsslapd-mdb-max-readers',
        'mdb_max_dbs': 'nsslapd-mdb-max-dbs',
        # VLV attributes
        'search_base': 'vlvbase',
        'search_scope': 'vlvscope',
        'search_filter': 'vlvfilter',
        'sort': 'vlvsort',
    }

SINGULAR = Backend
MANY = Backends
RDN = 'cn'
VLV_SEARCH_ATTRS = ['cn', 'vlvbase', 'vlvscope', 'vlvfilter']
VLV_INDEX_ATTRS = ['cn', 'vlvsort', 'vlvenabled', 'vlvuses']


def _args_to_attrs(args):
    attrs = {}
    for arg in vars(args):
        val = getattr(args, arg)
        if arg in arg_to_attr and val is not None:
            attrs[arg_to_attr[arg]] = val
    return attrs


def _search_backend_dn(inst, be_name):
    be_insts = MANY(inst).list()
    be_name = be_name.lower()
    for be in be_insts:
        cn = be.get_attr_val_utf8_l('cn')
        suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        if (is_a_dn(be_name) and str2dn(suffix) == str2dn(be_name)) or (not is_a_dn(be_name) and cn == be_name):
            return be.dn


def _get_backend(inst, be_name):
    be_insts = Backends(inst).list()
    be_name = be_name.lower()
    for be in be_insts:
        be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        cn = be.get_attr_val_utf8_l('cn')
        if (is_a_dn(be_name) and str2dn(be_suffix) == str2dn(be_name)) or (not is_a_dn(be_name) and cn == be_name):
            return be

    raise ValueError('Could not find backend suffix: {}'.format(be_name))


def _get_index(inst, be_name, attr):
    be_insts = Backends(inst).list()
    be_name = be_name.lower()
    attr = attr.lower()
    for be in be_insts:
        be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        cn = be.get_attr_val_utf8_l('cn')
        if (is_a_dn(be_name) and str2dn(be_suffix) == str2dn(be_name)) or (not is_a_dn(be_name) and cn == be_name):
            for index in be.get_indexes().list():
                idx_name = index.get_attr_val_utf8_l('cn')
                if idx_name == attr:
                    return index
    raise ValueError('Could not find index: {}'.format(attr))


def backend_list(inst, basedn, log, args):
    be_list = []
    be_insts = MANY(inst).list()
    for be in be_insts:
        suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        be_name = be.get_attr_val_utf8_l('cn')
        if args.skip_subsuffixes:
            # Skip subsuffixes
            mt = be._mts.get(suffix)
            sub = mt.get_attr_val_utf8_l('nsslapd-parent-suffix')
            if sub is not None:
                continue
        if args.suffix:
            val = "{}".format(suffix)
        else:
            val = "{} ({})".format(suffix, be_name)
        be_list.append(val)

    be_list.sort()
    if args.json:
        log.info(json.dumps({"type": "list", "items": be_list}, indent=4))
    else:
        if len(be_list) > 0:
            for be in be_list:
                log.info(be)
        else:
            log.info("No backends")


def backend_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    be = _get_backend(inst, rdn)
    bev = BackendSuffixView(inst, be)
    if args.json:
        entry = bev.get_all_attrs_json()
        entry_dict = json.loads(entry)
        log.info(json.dumps(entry_dict, indent=4))
    else:
        entry = bev.display()
        updated_entry = entry[:-1]  # remove \n
        log.info(updated_entry)


def backend_get_dn(inst, basedn, log, args):
    dn = _get_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('backend_get_dn'), MANY, dn, args)


def backend_create(inst, basedn, log, args):
    if not is_a_dn(args.suffix):
        raise ValueError("The suffix is not a valid DN")
    if args.parent_suffix:
        if not is_a_dn(args.parent_suffix):
            raise ValueError("The 'parent suffix' is not a valid DN")
        if not is_dn_parent(parent_dn=args.parent_suffix, child_dn=args.suffix):
            raise ValueError("The 'parent suffix' is not a parent of the backend suffix")
        props = {'cn': args.be_name, 'nsslapd-suffix': args.suffix, 'parent': args.parent_suffix}
    else:
        props = {'cn': args.be_name, 'nsslapd-suffix': args.suffix}
    if args.create_entries:
        props.update({BACKEND_SAMPLE_ENTRIES: INSTALL_LATEST_CONFIG})

    be = Backend(inst)
    be.create(properties=props)
    if args.create_suffix and not args.create_entries:
        # Set basic ACIs (taken from instance/setup.py)
        c_aci = '(targetattr="c || description || objectClass")(targetfilter="(objectClass=country)")(version 3.0; acl "Enable anyone c read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        o_aci = '(targetattr="o || description || objectClass")(targetfilter="(objectClass=organization)")(version 3.0; acl "Enable anyone o read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        dc_aci = '(targetattr="dc || description || objectClass")(targetfilter="(objectClass=domain)")(version 3.0; acl "Enable anyone domain read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        ou_aci = '(targetattr="ou || description || objectClass")(targetfilter="(objectClass=organizationalUnit)")(version 3.0; acl "Enable anyone ou read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        cn_aci = '(targetattr="cn || description || objectClass")(targetfilter="(objectClass=nscontainer)")(version 3.0; acl "Enable anyone cn read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
        suffix_rdn_attr = args.suffix.split('=')[0].lower()
        if suffix_rdn_attr == 'dc':
            domain = create_base_domain(inst, args.suffix)
            domain.add('aci', dc_aci)
        elif suffix_rdn_attr == 'o':
            org = create_base_org(inst, args.suffix)
            org.add('aci', o_aci)
        elif suffix_rdn_attr == 'ou':
            orgunit = create_base_orgunit(inst, args.suffix)
            orgunit.add('aci', ou_aci)
        elif suffix_rdn_attr == 'cn':
            cn = create_base_cn(inst, args.suffix)
            cn.add('aci', cn_aci)
        elif suffix_rdn_attr == 'c':
            c = create_base_c(inst, args.suffix)
            c.add('aci', c_aci)
        else:
            # Unsupported rdn
            raise ValueError("Suffix RDN is not supported for creating suffix object.  Only 'dc', 'o', 'ou', and 'cn' are supported.")

    log.info("The database was sucessfully created")


def _recursively_del_backends(be):
    sub_backends = be.get_sub_suffixes()
    if len(sub_backends) == 0:
        return

    for sub in sub_backends:
        has_subs = sub.get_sub_suffixes()
        if len(has_subs) > 0:
            _recursively_del_backends(sub)
        else:
            sub.delete()


def backend_delete(inst, basedn, log, args, warn=True):
    dn = _search_backend_dn(inst, args.be_name)
    if dn is None:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))
    if not args.ack:
        log.info("""Not removing backend: if you are really sure add: --do-it""")
    else:
        if warn and args.json is False:
            _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))

        be = _get_backend(inst, args.be_name)
        _recursively_del_backends(be)
        be.delete()

        log.info("The database, and any sub-suffixes, were successfully deleted")


def backend_import(inst, basedn, log, args):
    log = log.getChild('backend_import')
    dn = _search_backend_dn(inst, args.be_name)
    if dn is None:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))

    mc = SINGULAR(inst, dn)
    task = mc.import_ldif(ldifs=args.ldifs, chunk_size=args.chunks_size, encrypted=args.encrypted,
                          gen_uniq_id=args.gen_uniq_id, only_core=args.only_core, include_suffixes=args.include_suffixes,
                          exclude_suffixes=args.exclude_suffixes)
    task.wait(timeout=args.timeout)
    result = task.get_exit_code()
    warning = task.get_task_warn()

    if task.is_complete() and result == 0:
        if warning is None or (warning == 0):
            log.info("The import task has finished successfully")
        else:
            log.info("The import task has finished successfully, with warning code {}, check the logs for more detail".format(warning))
    else:
        if result is None:
            raise ValueError(f"Import task has not completed\n-------------------------\n{ensure_str(task.get_task_log())}")
        else:
            raise ValueError(f"Import task failed\n-------------------------\n{ensure_str(task.get_task_log())}")


def backend_export(inst, basedn, log, args):
    log = log.getChild('backend_export')

    # If the user gave a root suffix we need to get the backend CN
    be_cn_names = []
    if not isinstance(args.be_names, str):
        for be_name in args.be_names:
            dn = _search_backend_dn(inst, be_name)
            if dn is not None:
                mc = SINGULAR(inst, dn)
                be_cn_names.append(mc.rdn)
            else:
                raise ValueError("Unable to find a backend with the name: ({})".format(args.be_names))

    mc = MANY(inst)
    task = mc.export_ldif(be_names=be_cn_names, ldif=args.ldif, use_id2entry=args.use_id2entry,
                          encrypted=args.encrypted, min_base64=args.min_base64, no_dump_uniq_id=args.no_dump_uniq_id,
                          replication=args.replication, not_folded=args.not_folded, no_seq_num=args.no_seq_num,
                          include_suffixes=args.include_suffixes, exclude_suffixes=args.exclude_suffixes)
    task.wait(timeout=args.timeout)
    result = task.get_exit_code()

    if task.is_complete() and result == 0:
        log.info("The export task has finished successfully")
    else:
        if result is None:
            raise ValueError(f"Export task did not complete\n-------------------------\n{ensure_str(task.get_task_log())}")
        else:
            raise ValueError(f"Export task failed\n-------------------------\n{ensure_str(task.get_task_log())}")


def is_db_link(inst, rdn):
    links = ChainingLinks(inst).list()
    for link in links:
        cn = link.get_attr_val_utf8_l('cn')
        if cn == rdn.lower():
            return True
    return False


def is_db_replicated(inst, suffix):
    replicas = Replicas(inst)
    try:
        replicas.get(suffix)
        return True
    except:
        return False


def backend_get_subsuffixes(inst, basedn, log, args):
    subsuffixes = []
    be_insts = MANY(inst).list()
    for be in be_insts:
        be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        if be_suffix == args.be_name.lower():
            # We have our parent, now find the children
            mts = be._mts.list()
            for mt in mts:
                db_type = "suffix"
                sub = mt.get_attr_val_utf8_l('nsslapd-parent-suffix')
                sub_be = mt.get_attr_val_utf8_l('nsslapd-backend')
                if sub == be_suffix:
                    # We have a subsuffix (maybe a db link?)
                    if is_db_link(inst, sub_be):
                        db_type = "link"

                    if args.suffix:
                        subsuffixes.append(mt.get_attr_val_utf8_l('cn'))
                    else:
                        if args.json:
                            val = {"suffix": mt.get_attr_val_utf8_l('cn'),
                                   "backend": mt.get_attr_val_utf8_l('nsslapd-backend'),
                                   "type": db_type}
                        else:
                            val = ("{} ({}) Database Type: {}".format(
                                mt.get_attr_val_utf8_l('cn'),
                                mt.get_attr_val_utf8_l('nsslapd-backend'),
                                db_type))
                        subsuffixes.append(val)
            break
    if len(subsuffixes) > 0:
        subsuffixes.sort()
        if args.json:
            log.info(json.dumps({"type": "list", "items": subsuffixes}, indent=4))
        else:
            for sub in subsuffixes:
                log.info(sub)
    else:
        if args.json:
            log.info(json.dumps({"type": "list", "items": []}, indent=4))
        else:
            log.info("No sub-suffixes under this backend")


def build_node(suffix, be_name, subsuf=False, link=False, replicated=False):
    """Build the UI node for a suffix
    """
    suffix_type = "suffix"
    if subsuf:
        suffix_type = "subsuffix"
    if link:
        suffix_type = "dblink"

    return {
        "name": suffix,
        "id": suffix,
        "type": suffix_type,
        "replicated": replicated,
        "be": be_name,
        "children": []
    }


def backend_build_tree(inst, be_insts, nodes):
    """Recursively build the tree
    """
    if len(nodes) == 0:
        # Done
        return

    for node in nodes:
        node_suffix = node['id']
        # Get sub suffixes and chaining of node
        for be in be_insts:
            be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
            if be_suffix == node_suffix.lower():
                # We have our parent, now find the children
                mts = be._mts.list()

                for mt in mts:
                    sub_parent = mt.get_attr_val_utf8_l('nsslapd-parent-suffix')
                    sub_be = mt.get_attr_val_utf8_l('nsslapd-backend')
                    sub_suffix = mt.get_attr_val_utf8_l('cn')
                    if sub_parent == be_suffix:
                        # We have a subsuffix (maybe a db link?)
                        link = is_db_link(inst, sub_be)
                        replicated = is_db_replicated(inst, sub_suffix)
                        node['children'].append(build_node(sub_suffix,
                                                        sub_be,
                                                        subsuf=True,
                                                        link=link,
                                                        replicated=replicated))

                # Recurse over the new subsuffixes
                backend_build_tree(inst, be_insts, node['children'])
                break


def print_suffix_tree(nodes, level, log):
    """Print all the nodes and children recursively
    """
    if len(nodes) > 0:
        for node in nodes:
            spaces = " " * level
            log.info('{}- {}'.format(spaces, node['id']))
            if len(node['children']) > 0:
                print_suffix_tree(node['children'], level + 2, log)


def backend_get_tree(inst, basedn, log, args):
    """Build a tree model of all the suffixes/sub suffixes and DB links
    """
    nodes = []

    # Get the top suffixes
    be_insts = MANY(inst).list()
    for be in be_insts:
        suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
        be_name = be.get_attr_val_utf8('cn')
        mt = be._mts.get(suffix)
        sub = mt.get_attr_val_utf8_l('nsslapd-parent-suffix')
        if sub is not None:
            continue
        replicated = is_db_replicated(inst, suffix)
        nodes.append(build_node(suffix, be_name, replicated=replicated))

    # No suffixes, return empty list
    if len(nodes) == 0:
        if args.json:
            log.info(json.dumps(nodes, indent=4))
        else:
            log.info("There are no suffixes defined")
    else:
        # Build the tree
        be_insts = Backends(inst).list()
        backend_build_tree(inst, be_insts, nodes)

        # Done
        if args.json:
            log.info(json.dumps(nodes, indent=4))
        else:
            print_suffix_tree(nodes, 1, log)


def backend_set(inst, basedn, log, args):
    # Validate paried args
    if args.enable and args.disable:
        raise ValueError("You can not enable and disable a backend at the same time")
    if args.enable_readonly and args.disable_readonly:
        raise ValueError("You can not set the backend readonly mode to be both enabled and disabled at the same time")
    if args.enable_orphan and args.disable_orphan:
        raise ValueError("You can not set the backend orphan mode to be both enabled and disabled at the same time")

    # Update backend
    need_restart = False
    be = _get_backend(inst, args.be_name)
    bev = BackendSuffixView(inst, be)
    if args.enable_readonly:
        bev.set('nsslapd-readonly', 'on')
    if args.disable_readonly:
        bev.set('nsslapd-readonly', 'off')
    if args.enable_orphan:
        bev.set('orphan', 'true')
        need_restart = True
    if args.disable_orphan:
        bev.set('orphan', 'false')
        need_restart = True
    if args.add_referral:
        bev.add('nsslapd-referral', args.add_referral)
    if args.del_referral:
        bev.remove('nsslapd-referral', args.del_referral)
    if args.cache_size:
        bev.set('nsslapd-cachesize', args.cache_size)
    if args.cache_memsize:
        bev.set('nsslapd-cachememsize', args.cache_memsize)
    if args.cache_preserved_entries:
        bev.set('nsslapd-cache-preserved-entries', args.cache_preserved_entries)
    if args.dncache_memsize:
        bev.set('nsslapd-dncachememsize', args.dncache_memsize)
    if args.require_index:
        bev.set('nsslapd-require-index', 'on')
    if args.ignore_index:
        bev.set('nsslapd-require-index', 'off')
    if args.state:
        bev.set_state(args.state)
    if args.enable:
        be.enable()
    if args.disable:
        be.disable()
    log.info("The backend configuration was successfully updated")
    if need_restart:
        log.warn("Warning! The server instance must be restarted to take in account that configuration change.")


def db_config_get(inst, basedn, log, args):
    db_cfg = DatabaseConfig(inst)
    if args.json:
        log.info(json.dumps({"type": "entry", "attrs": db_cfg.get()}, indent=4))
    else:
        db_cfg.display()


def db_config_set(inst, basedn, log, args):
    db_cfg = DatabaseConfig(inst)
    attrs = _args_to_attrs(args)
    did_something = False
    replace_list = []

    for attr, value in list(attrs.items()):
        if value == "":
            # We don't support deleting attributes or setting empty values in db
            continue
        else:
            replace_list.append([attr, value])
    if len(replace_list) > 0:
        db_cfg.set(replace_list)
    elif not did_something:
        raise ValueError("There are no changes to set in the database configuration")

    log.info("Successfully updated database configuration")


def get_monitor(inst, basedn, log, args):
    if args.suffix is not None:
        # Get a suffix/backend monitor entry
        be = _get_backend(inst, args.suffix)
        monitor = be.get_monitor()
        if monitor is not None:
            _format_status(log, monitor, args.json)
            return
        raise ValueError("There was no monitor found for suffix '{}'".format(args.suffix))
    else:
        # Get the global database monitor entry
        monitor = MonitorLDBM(inst)
        _format_status(log, monitor, args.json)


def backend_add_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    be.add_index(args.attr, args.index_type, args.matching_rule, reindex=args.reindex)
    log.info("Successfully added index")


def backend_set_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    index = _get_index(inst, args.be_name, args.attr)
    if args.add_type is not None:
        for add_type in args.add_type:
            index.add('nsIndexType', add_type)
    if args.del_type is not None:
        for del_type in args.del_type:
            try:
                index.remove('nsIndexType', del_type)
            except ldap.NO_SUCH_ATTRIBUTE:
                raise ValueError('Can not delete index type because it does not exist')
    if args.add_mr is not None:
        for add_mr in args.add_mr:
            index.add('nsMatchingRule', add_mr)
    if args.del_mr is not None:
        for del_mr in args.del_mr:
            try:
                index.remove('nsMatchingRule', del_mr)
            except ldap.NO_SUCH_ATTRIBUTE:
                raise ValueError('Can not delete matching rule type because it does not exist')

    if args.reindex:
        be.reindex(attrs=[args.attr])
    log.info("Index successfully updated")


def backend_get_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    results = []
    for attr in args.attr:
        index = be.get_index(attr)
        if index is not None:
            if args.json:
                entry = index.get_all_attrs_json()
                # Append decoded json object, because we are going to dump it later
                results.append(json.loads(entry))
            else:
                log.info(index.display())
    if args.json:
        log.info(json.dumps({"type": "list", "items": results}, indent=4))


def backend_list_index(inst, basedn, log, args):
    results = []
    be = _get_backend(inst, args.be_name)
    indexes = be.get_indexes().list()
    for index in indexes:
        if args.json:
            if args.just_names:
                results.append(index.get_attr_val_utf8_l('cn'))
            else:
                results.append(json.loads(index.get_all_attrs_json()))
        else:
            if args.just_names:
                log.info(index.get_attr_val_utf8_l('cn'))
            else:
                log.info(index.display())

    if args.json:
        log.info(json.dumps({"type": "list", "items": results}, indent=4))


def backend_del_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    for attr in args.attr:
        be.del_index(attr)
        log.info("Successfully deleted index \"{}\"".format(attr))


def backend_reindex(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    be.reindex(attrs=args.attr, wait=args.wait)
    log.info("Successfully reindexed database")


def backend_attr_encrypt(inst, basedn, log, args):
    # add/remove/list
    be = _get_backend(inst, args.be_name)

    if args.add_attr is not None:
        for attr in args.add_attr:
            be.add_encrypted_attr(attr)
        if len(args.add_attr) > 1:
            log.info("Successfully added encrypted attributes")
        else:
            log.info("Successfully added encrypted attribute")
    if args.del_attr is not None:
        for attr in args.del_attr:
            be.del_encrypted_attr(attr)
        if len(args.del_attr) > 1:
            log.info("Successfully deleted encrypted attributes")
        else:
            log.info("Successfully deleted encrypted attribute")
    if args.list:
        results = be.get_encrypted_attrs(args.just_names)
        if args.json:
            json_results = []
            if args.just_names:
                json_results = results
            else:
                for result in results:
                    json_results.append(json.loads(result.get_all_attrs_json()))
            log.info(json.dumps({"type": "list", "items": json_results}, indent=4))

        else:
            if len(results) == 0:
                log.info("There are no encrypted attributes for this backend")
            else:
                for attr in results:
                    if args.just_names:
                        log.info(attr)
                    else:
                        log.info(attr.display())


def backend_list_vlv(inst, basedn, log, args):
    # Get the searches
    results = []
    be = _get_backend(inst, args.be_name)
    vlvs = be.get_vlv_searches()
    for vlv in vlvs:
        if args.json:
            if args.just_names:
                results.append(vlv.get_attr_val_utf8_l('cn'))
            else:
                entry = vlv.get_attrs_vals_json(VLV_SEARCH_ATTRS)
                indexes = vlv.get_sorts()
                sorts = []
                for idx in indexes:
                    index_entry = idx.get_attrs_vals_json(VLV_INDEX_ATTRS)
                    sorts.append(json.loads(index_entry))
                entry = json.loads(entry)  # Return entry to a dict
                entry["sorts"] = sorts  # Update dict
                results.append(entry)
        else:
            if args.just_names:
                log.info(vlv.get_attr_val_utf8_l('cn'))
            else:
                raw_entry = vlv.get_attrs_vals(VLV_SEARCH_ATTRS)
                log.info('dn: ' + vlv.dn)
                for k, v in list(raw_entry.items()):
                    log.info('{}: {}'.format(ensure_str(k), ensure_str(v[0])))
                indexes = vlv.get_sorts()
                sorts = []
                log.info("Sorts:")
                for idx in indexes:
                    entry = idx.get_attrs_vals(VLV_INDEX_ATTRS)
                    log.info(' - dn: ' + idx.dn)
                    for k, v in list(entry.items()):
                        log.info(' - {}: {}'.format(ensure_str(k), ensure_str(v[0])))

    if args.json:
        log.info(json.dumps({"type": "list", "items": results}, indent=4))


def backend_get_vlv(inst, basedn, log, args):
    results = []
    be = _get_backend(inst, args.be_name)
    vlvs = be.get_vlv_searches()
    for vlv in vlvs:
        vlv_name = vlv.get_attr_val_utf8_l('cn')
        if vlv_name == args.name.lower():
            if args.json:
                entry = vlv.get_attrs_vals_json(VLV_SEARCH_ATTRS)
                results.append(json.loads(entry))
            else:
                raw_entry = vlv.get_attrs_vals(VLV_SEARCH_ATTRS)
                log.info('dn: ' + vlv._dn)
                for k, v in list(raw_entry.items()):
                    log.info('{}: {}'.format(ensure_str(k), ensure_str(v[0])))
            # Print indexes
            indexes = vlv.get_sorts()
            for idx in indexes:
                if args.json:
                    entry = idx.get_attrs_vals_json(VLV_INDEX_ATTRS)
                    results.append(json.loads(entry))
                else:
                    raw_entry = idx.get_attrs_vals(VLV_INDEX_ATTRS)
                    log.info('Sorts:')
                    log.info(' - dn: ' + idx._dn)
                    for k, v in list(raw_entry.items()):
                        log.info(' - {}: {}'.format(ensure_str(k), ensure_str(v[0])))
                    log.info()

            if args.json:
                log.info(json.dumps({"type": "list", "items": results}, indent=4))


def backend_create_vlv(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    props = {'cn': args.name,
             'vlvbase': args.search_base,
             'vlvscope': args.search_scope,
             'vlvfilter': args.search_filter}
    be.add_vlv_search(args.name, props)
    log.info("Successfully created new VLV Search entry, now you can add indexes to it.")


def backend_edit_vlv(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    attrs = _args_to_attrs(args)
    replace_list = []

    vlv_search = be.get_vlv_searches(vlv_name=args.name)

    for attr, value in list(attrs.items()):
        if value == "":
            # We don't support deleting attributes or setting empty values
            continue
        else:
            replace_list.append((attr, value))
    if len(replace_list) > 0:
        vlv_search.replace_many(*replace_list)
    else:
        raise ValueError("There are no changes to set in the VLV search entry")
    if args.reindex:
        vlv_search.reindex()
    log.info("Successfully updated VLV search entry")


def backend_del_vlv(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    vlv_search = be.get_vlv_searches(vlv_name=args.name)
    vlv_search.delete_all()
    log.info("Successfully deleted VLV search and its indexes")


def backend_create_vlv_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    vlv_search = be.get_vlv_searches(vlv_name=args.parent_name)
    vlv_search.add_sort(args.index_name, args.sort)
    if args.index_it:
        vlv_search.reindex(args.be_name, vlv_index=args.index_name)
    log.info("Successfully created new VLV index entry")


def backend_delete_vlv_index(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    vlv_search = be.get_vlv_searches(vlv_name=args.parent_name)
    vlv_search.delete_sort(args.index_name, args.sort)
    log.info("Successfully deleted VLV index entry")


def backend_reindex_vlv(inst, basedn, log, args):
    be = _get_backend(inst, args.be_name)
    suffix = be.get_suffix()
    vlv_search = be.get_vlv_searches(vlv_name=args.parent_name)
    vlv_search.reindex(suffix, vlv_index=args.index_name)
    log.info("Successfully reindexed VLV indexes")


def backend_compact(inst, basedn, log, args):
    task = DBCompactTask(inst)
    task_properties = {}
    if args.only_changelog:
        task_properties = {'justChangelog': 'yes'}
    task.create(properties=task_properties)
    task.wait(timeout=args.timeout)
    result = task.get_exit_code()
    if result != 0:
        if result is None:
            raise ValueError("Database Compaction Task has not completed")
        else:
            raise ValueError(f"Database Compaction Task failed, error: {result}")

    log.info("Successfully started Database Compaction Task")


def create_parser(subparsers):
    backend_parser = subparsers.add_parser('backend', help="Manage database suffixes and backends", formatter_class=CustomHelpFormatter)
    subcommands = backend_parser.add_subparsers(help="action")

    #####################################################
    # Suffix parser
    #####################################################
    suffix_parser = subcommands.add_parser('suffix', help="Manage backend suffixes", formatter_class=CustomHelpFormatter)
    suffix_subcommands = suffix_parser.add_subparsers(help="action")

    # List backends/suffixes
    list_parser = suffix_subcommands.add_parser('list', help="List active backends and suffixes", formatter_class=CustomHelpFormatter)
    list_parser.set_defaults(func=backend_list)
    list_parser.add_argument('--suffix', action='store_true', help='Displays the suffixes without backend name')
    list_parser.add_argument('--skip-subsuffixes', action='store_true', help='Displays the list of suffixes without sub-suffixes')

    # Get backend
    get_parser = suffix_subcommands.add_parser('get', help='Display the suffix entry', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=backend_get)
    get_parser.add_argument('selector', nargs='?', help='The backend database name to search for')

    # Get the DN of a backend
    get_dn_parser = suffix_subcommands.add_parser('get-dn', help='Display the DN of a backend', formatter_class=CustomHelpFormatter)
    get_dn_parser.set_defaults(func=backend_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The DN to the database entry in cn=ldbm database,cn=plugins,cn=config')

    # Get subsuffixes
    get_subsuffix_parser = suffix_subcommands.add_parser('get-sub-suffixes', help='Display sub-suffixes', formatter_class=CustomHelpFormatter)
    get_subsuffix_parser.set_defaults(func=backend_get_subsuffixes)
    get_subsuffix_parser.add_argument('--suffix', action='store_true', help='Displays the list of suffixes without backend name')
    get_subsuffix_parser.add_argument('be_name', help='The backend name or suffix')

    # Set the backend/suffix configuration
    set_backend_parser = suffix_subcommands.add_parser('set', help='Set configuration settings for a specific backend', formatter_class=CustomHelpFormatter)
    set_backend_parser.set_defaults(func=backend_set)
    set_backend_parser.add_argument('--enable-readonly', action='store_true', help='Enables read-only mode for the backend database')
    set_backend_parser.add_argument('--disable-readonly', action='store_true', help='Disables read-only mode for the backend database')
    set_backend_parser.add_argument('--enable-orphan', action='store_true', help='Disconnect a subsuffix from its parent suffix.')
    set_backend_parser.add_argument('--disable-orphan', action='store_true', help='Let the subsuffix be connected to its parent suffix.')
    set_backend_parser.add_argument('--require-index', action='store_true', help='Allows only indexed searches')
    set_backend_parser.add_argument('--ignore-index', action='store_true', help='Allows all searches even if they are unindexed')
    set_backend_parser.add_argument('--add-referral', help='Adds an LDAP referral to the backend')
    set_backend_parser.add_argument('--del-referral', help='Removes an LDAP referral from the backend')
    set_backend_parser.add_argument('--enable', action='store_true', help='Enables the backend database')
    set_backend_parser.add_argument('--disable', action='store_true', help='Disables the backend database')
    set_backend_parser.add_argument('--cache-size', help='Sets the maximum number of entries to keep in the entry cache')
    set_backend_parser.add_argument('--cache-memsize', help='Sets the maximum size in bytes that the entry cache can grow to')
    set_backend_parser.add_argument('--cache-preserved-entries', help='Sets the maximum number of entries that are not evicted from the cache when trying to make space. This is typically used to keep very large groups in the cache')
    set_backend_parser.add_argument('--dncache-memsize', help='Sets the maximum size in bytes that the DN cache can grow to')
    set_backend_parser.add_argument('--state', help='Changes the backend state to: "backend", "disabled", "referral", or "referral on update"')
    set_backend_parser.add_argument('be_name', help='The backend name or suffix')

    #########################################
    # Index parser
    #########################################
    index_parser = subcommands.add_parser('index', help="Manage backend indexes", formatter_class=CustomHelpFormatter)
    index_subcommands = index_parser.add_subparsers(help="action")

    # Create index
    add_index_parser = index_subcommands.add_parser('add', help='Add an index', formatter_class=CustomHelpFormatter)
    add_index_parser.set_defaults(func=backend_add_index)
    add_index_parser.add_argument('--index-type', required=True, action='append', help='Sets the indexing type (eq, sub, pres, or approx)')
    add_index_parser.add_argument('--matching-rule', action='append', help='Sets the matching rule for the index')
    add_index_parser.add_argument('--reindex', action='store_true', help='Re-indexes the database after adding a new index')
    add_index_parser.add_argument('--attr', required=True, help='Sets the attribute name to index')
    add_index_parser.add_argument('be_name', help='The backend name or suffix')

    # Edit index
    edit_index_parser = index_subcommands.add_parser('set', help='Update an index', formatter_class=CustomHelpFormatter)
    edit_index_parser.set_defaults(func=backend_set_index)
    edit_index_parser.add_argument('--attr', required=True, help='Sets the indexed attribute to update')
    edit_index_parser.add_argument('--add-type', action='append', help='Adds an index type to the index (eq, sub, pres, or approx)')
    edit_index_parser.add_argument('--del-type', action='append', help='Removes an index type from the index: (eq, sub, pres, or approx)')
    edit_index_parser.add_argument('--add-mr', action='append', help='Adds a matching-rule to the index')
    edit_index_parser.add_argument('--del-mr', action='append', help='Removes a matching-rule from the index')
    edit_index_parser.add_argument('--reindex', action='store_true', help='Re-indexes the database after editing the index')
    edit_index_parser.add_argument('be_name', help='The backend name or suffix')

    # Get index
    get_index_parser = index_subcommands.add_parser('get', help='Display an index entry', formatter_class=CustomHelpFormatter)
    get_index_parser.set_defaults(func=backend_get_index)
    get_index_parser.add_argument('--attr', required=True, action='append', help='Sets the index name to display')
    get_index_parser.add_argument('be_name', help='The backend name or suffix')

    # list indexes
    list_index_parser = index_subcommands.add_parser('list', help='Display the index', formatter_class=CustomHelpFormatter)
    list_index_parser.set_defaults(func=backend_list_index)
    list_index_parser.add_argument('--just-names', action='store_true', help='Displays only the names of indexed attributes')
    list_index_parser.add_argument('be_name', help='The backend name or suffix')

    # Delete index
    del_index_parser = index_subcommands.add_parser('delete', help='Delete an index', formatter_class=CustomHelpFormatter)
    del_index_parser.set_defaults(func=backend_del_index)
    del_index_parser.add_argument('--attr', action='append', help='Sets the name of the attribute to delete from the index')
    del_index_parser.add_argument('be_name', help='The backend name or suffix')

    # reindex index
    reindex_parser = index_subcommands.add_parser('reindex', help='Re-index the database for a single index or all indexes', formatter_class=CustomHelpFormatter)
    reindex_parser.set_defaults(func=backend_reindex)
    reindex_parser.add_argument('--attr', action='append', help='Sets the name of the attribute to re-index. Omit this argument to re-index all attributes')
    reindex_parser.add_argument('--wait', action='store_true', help='Waits for the index task to complete and reports the status')
    reindex_parser.add_argument('be_name', help='The backend name or suffix')

    #############################################
    # VLV parser
    #############################################
    vlv_parser = subcommands.add_parser('vlv-index', help="Manage VLV searches and indexes", formatter_class=CustomHelpFormatter)
    vlv_subcommands = vlv_parser.add_subparsers(help="action")

    # List VLV Searches
    list_vlv_search_parser = vlv_subcommands.add_parser('list', help='List VLV search and index entries', formatter_class=CustomHelpFormatter)
    list_vlv_search_parser.set_defaults(func=backend_list_vlv)
    list_vlv_search_parser.add_argument('--just-names', action='store_true', help='Displays only the names of VLV search entries')
    list_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Get VLV search entry and indexes
    get_vlv_search_parser = vlv_subcommands.add_parser('get', help='Display a VLV search and indexes', formatter_class=CustomHelpFormatter)
    get_vlv_search_parser.set_defaults(func=backend_get_vlv)
    get_vlv_search_parser.add_argument('--name', help='Displays the VLV search entry and its index entries')
    get_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Create VLV Search
    add_vlv_search_parser = vlv_subcommands.add_parser('add-search', help='Add a VLV search entry. The search entry is the parent entry '
                                                                          'of the VLV index entries, and it specifies the search parameters that '
                                                                          'are used to match entries for those indexes.')
    add_vlv_search_parser.set_defaults(func=backend_create_vlv)
    add_vlv_search_parser.add_argument('--name', required=True, help='Sets the name of the VLV search entry')
    add_vlv_search_parser.add_argument('--search-base', required=True, help='Sets the VLV search base')
    add_vlv_search_parser.add_argument('--search-scope', required=True, help='Sets the VLV search scope: 0 (base search), 1 (one-level search), or 2 (subtree search)')
    add_vlv_search_parser.add_argument('--search-filter', required=True, help='Sets the VLV search filter')
    add_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Edit vlv search
    edit_vlv_search_parser = vlv_subcommands.add_parser('edit-search', help='Update a VLV search and index', formatter_class=CustomHelpFormatter)
    edit_vlv_search_parser.set_defaults(func=backend_edit_vlv)
    edit_vlv_search_parser.add_argument('--name', required=True, help='Sets the name of the VLV index')
    edit_vlv_search_parser.add_argument('--search-base', help='Sets the VLV search base')
    edit_vlv_search_parser.add_argument('--search-scope', help='Sets the VLV search scope: 0 (base search), 1 (one-level search), or 2 (subtree search)')
    edit_vlv_search_parser.add_argument('--search-filter', help='Sets the VLV search filter')
    edit_vlv_search_parser.add_argument('--reindex', action='store_true', help='Re-indexes all VLV database indexes')
    edit_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index to update')

    # Delete vlv search(and index)
    del_vlv_search_parser = vlv_subcommands.add_parser('del-search', help='Delete VLV search & index', formatter_class=CustomHelpFormatter)
    del_vlv_search_parser.set_defaults(func=backend_del_vlv)
    del_vlv_search_parser.add_argument('--name', required=True, help='Sets the name of the VLV search index')
    del_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Create VLV Index
    add_vlv_index_parser = vlv_subcommands.add_parser('add-index', help='Create a VLV index under a VLV search entry (parent entry, formatter_class=CustomHelpFormatter). '
                                                                        'The VLV index specifies the attributes to sort')
    add_vlv_index_parser.set_defaults(func=backend_create_vlv_index)
    add_vlv_index_parser.add_argument('--parent-name', required=True, help='Sets the name or "cn" attribute of the parent VLV search entry')
    add_vlv_index_parser.add_argument('--index-name', required=True, help='Sets the name of the new VLV index')
    add_vlv_index_parser.add_argument('--sort', required=True, help='Sets a space-separated list of attributes to sort for this VLV index')
    add_vlv_index_parser.add_argument('--index-it', action='store_true', help='Creates the database index for this VLV index definition')
    add_vlv_index_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Delete VLV Index
    del_vlv_index_parser = vlv_subcommands.add_parser('del-index', help='Delete a VLV index under a VLV search entry (parent entry)', formatter_class=CustomHelpFormatter)
    del_vlv_index_parser.set_defaults(func=backend_delete_vlv_index)
    del_vlv_index_parser.add_argument('--parent-name', required=True, help='Sets the name or "cn" attribute value of the parent VLV search entry')
    del_vlv_index_parser.add_argument('--index-name', help='Sets the name of the VLV index to delete')
    del_vlv_index_parser.add_argument('--sort', help='Delete a VLV index that has this vlvsort value')
    del_vlv_index_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Reindex VLV
    reindex_vlv_parser = vlv_subcommands.add_parser('reindex', help='Index/re-index the VLV database index', formatter_class=CustomHelpFormatter)
    reindex_vlv_parser.set_defaults(func=backend_reindex_vlv)
    reindex_vlv_parser.add_argument('--index-name', help='Sets the name of the VLV index entry to re-index. If not set, all indexes are re-indexed')
    reindex_vlv_parser.add_argument('--parent-name', required=True, help='Sets the name or "cn" attribute value of the parent VLV search entry')
    reindex_vlv_parser.add_argument('be_name', help='The backend name of the VLV index')

    ############################################
    # Encrypted Attributes
    ############################################
    attr_encrypt_parser = subcommands.add_parser('attr-encrypt', help='Manage encrypted attribute settings', formatter_class=CustomHelpFormatter)
    attr_encrypt_parser.set_defaults(func=backend_attr_encrypt)
    attr_encrypt_parser.add_argument('--list', action='store_true', help='Lists all encrypted attributes in the backend')
    attr_encrypt_parser.add_argument('--just-names', action='store_true', help='List only the names of the encrypted attributes when used with --list')
    attr_encrypt_parser.add_argument('--add-attr', action='append', help='Enables encryption for the specified attribute')
    attr_encrypt_parser.add_argument('--del-attr', action='append', help='Disables encryption for the specified attribute')
    attr_encrypt_parser.add_argument('be_name', help='The backend name or suffix')

    ############################################
    # Global DB Config
    ############################################
    db_parser = subcommands.add_parser('config', help="Manage the global database configuration settings", formatter_class=CustomHelpFormatter)
    db_subcommands = db_parser.add_subparsers(help="action")

    # Get the global database configuration
    get_db_config_parser = db_subcommands.add_parser('get', help='Display the global database configuration', formatter_class=CustomHelpFormatter)
    get_db_config_parser.set_defaults(func=db_config_get)

    # Update the global database configuration
    set_db_config_parser = db_subcommands.add_parser('set', help='Set the global database configuration', formatter_class=CustomHelpFormatter)
    set_db_config_parser.set_defaults(func=db_config_set)
    set_db_config_parser.add_argument('--lookthroughlimit', help='Specifies the maximum number of entries that the server '
                                                                 'will check when examining candidate entries in response to a search request')
    set_db_config_parser.add_argument('--mode', help='Specifies the permissions used for newly created index files')
    set_db_config_parser.add_argument('--idlistscanlimit', help='Specifies the number of entry IDs that are searched during a search operation')
    set_db_config_parser.add_argument('--directory', help='Specifies absolute path to database instance')
    set_db_config_parser.add_argument('--dbcachesize', help='Specifies the database index cache size in bytes')
    set_db_config_parser.add_argument('--logdirectory', help='Specifies the path to the directory that contains the database transaction logs')
    set_db_config_parser.add_argument('--txn-wait', help='Sets whether the server should should wait if there are no db locks available')
    set_db_config_parser.add_argument('--checkpoint-interval', help='Sets the amount of time in seconds after which the server sends a '
                                                                    'checkpoint entry to the database transaction log')
    set_db_config_parser.add_argument('--compactdb-interval', help='Sets the interval in seconds when the database is compacted')
    set_db_config_parser.add_argument('--compactdb-time', help='Sets the time (HH:MM format) of day when to compact the database after the "compactdb interval" has been reached')
    set_db_config_parser.add_argument('--txn-batch-val', help='Specifies how many transactions will be batched before being committed')
    set_db_config_parser.add_argument('--txn-batch-min', help='Controls when transactions should be flushed earliest, independently of '
                                                              'the batch count. Requires that txn-batch-val is set')
    set_db_config_parser.add_argument('--txn-batch-max', help='Controls when transactions should be flushed latest, independently of '
                                                              'the batch count. Requires that txn-batch-val is set)')
    set_db_config_parser.add_argument('--logbufsize', help='Specifies the transaction log information buffer size')
    set_db_config_parser.add_argument('--locks', help='Sets the maximum number of database locks')
    set_db_config_parser.add_argument('--locks-monitoring-enabled', help='Enables or disables monitoring of DB locks when the value crosses the percentage '
                                                                         'set with "--locks-monitoring-threshold"')
    set_db_config_parser.add_argument('--locks-monitoring-threshold', help='Sets the DB lock exhaustion threshold in percentage (valid range is 70-90). '
                                                                           'When the threshold is reached, all searches are aborted until the number of active '
                                                                           'locks decreases below the configured threshold and/or the '
                                                                           'administrator increases the number of database locks (nsslapd-db-locks). '
                                                                           'This threshold is a safeguard against DB corruption which might be caused '
                                                                           'by locks exhaustion.')
    set_db_config_parser.add_argument('--locks-monitoring-pause', help='Sets the DB lock monitoring value in milliseconds for the amount of time '
                                                                       'that the monitoring thread spends waiting between checks.')
    set_db_config_parser.add_argument('--import-cache-autosize', help='Enables or disables to automatically set the size of the import '
                                                                       'cache to be used during the import process of LDIF files')
    set_db_config_parser.add_argument('--cache-autosize', help='Sets the percentage of free memory that is used in total for the database '
                                                               'and entry cache. "0" disables this feature.')
    set_db_config_parser.add_argument('--cache-autosize-split', help='Sets the percentage of RAM that is used for the database cache. The '
                                                                     'remaining percentage is used for the entry cache')
    set_db_config_parser.add_argument('--import-cachesize', help='Sets the size in bytes of the database cache used in the import process.')
    set_db_config_parser.add_argument('--exclude-from-export', help='List of attributes to not include during database export operations')
    set_db_config_parser.add_argument('--pagedlookthroughlimit', help='Specifies the maximum number of entries that the server '
                                                                      'will check when examining candidate entries for a search which uses '
                                                                      'the simple paged results control')
    set_db_config_parser.add_argument('--pagedidlistscanlimit', help='Specifies the number of entry IDs that are searched, specifically, '
                                                                     'for a search operation using the simple paged results control.')
    set_db_config_parser.add_argument('--rangelookthroughlimit', help='Specifies the maximum number of entries that the server '
                                                                      'will check when examining candidate entries in response to a '
                                                                      'range search request.')
    set_db_config_parser.add_argument('--backend-opt-level', help='Sets the backend optimization level for write performance (0, 1, 2, or 4). '
                                                                  'WARNING: This parameter can trigger experimental code.')
    set_db_config_parser.add_argument('--deadlock-policy', help='Adjusts the backend database deadlock policy (Advanced setting)')
    set_db_config_parser.add_argument('--db-home-directory', help='Sets the directory for the database mmapped files (Advanced setting)')
    set_db_config_parser.add_argument('--db-lib', help='Sets which db lib is used. Valid values are: bdb or mdb')
    set_db_config_parser.add_argument('--mdb-max-size', help='Sets the lmdb database maximum size (in bytes).')
    set_db_config_parser.add_argument('--mdb-max-readers', help='Sets the lmdb database maximum number of readers (Advanced setting)')
    set_db_config_parser.add_argument('--mdb-max-dbs', help='Sets the lmdb database maximum number of sub databases (Advanced setting)')


    #######################################################
    # Database & Suffix Monitor
    #######################################################
    get_monitor_parser = subcommands.add_parser('monitor', help="Displays global database or suffix monitoring information", formatter_class=CustomHelpFormatter)
    get_monitor_parser.set_defaults(func=get_monitor)
    get_monitor_parser.add_argument('--suffix', help='Displays monitoring information only for the specified suffix')

    #######################################################
    # Import LDIF
    #######################################################
    import_parser = subcommands.add_parser('import', help="Online import of a suffix", formatter_class=CustomHelpFormatter)
    import_parser.set_defaults(func=backend_import)
    import_parser.add_argument('be_name', nargs='?',
                               help='The backend name or the root suffix')
    import_parser.add_argument('ldifs', nargs='*',
                               help="Specifies the filename of the input LDIF files. "
                                    "Multiple files are imported in the specified order.")
    import_parser.add_argument('-c', '--chunks-size', type=int,
                               help="The number of chunks to have during the import operation")
    import_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="Encrypt attributes configured in the database for encryption")
    import_parser.add_argument('-g', '--gen-uniq-id',
                               help="Generate a unique id. Set \"none\" for no unique ID to be generated "
                                    "and \"deterministic\" for the generated unique ID to be name-based. "
                                    "By default, a time-based unique ID is generated. "
                                    "When using the deterministic generation to have a name-based unique ID, "
                                    "it is also possible to specify the namespace for the server to use. "
                                    "namespaceId is a string of characters "
                                    "in the format 00-xxxxxxxx-xxxxxxxx-xxxxxxxx-xxxxxxxx.")
    import_parser.add_argument('-O', '--only-core', action='store_true',
                               help="Creates only the core database attribute indexes")
    import_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included")
    import_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded")
    import_parser.add_argument('--timeout', type=int, default=0,
                               help="Set a timeout to wait for the export task.  Default is 0 (no timeout)")

    #######################################################
    # Export LDIF
    #######################################################
    export_parser = subcommands.add_parser('export', help='Online export of a suffix', formatter_class=CustomHelpFormatter)
    export_parser.set_defaults(func=backend_export)
    export_parser.add_argument('be_names', nargs='+',
                               help="The backend names or the root suffixes")
    export_parser.add_argument('-l', '--ldif',
                               help="Sets the filename of the output LDIF file. "
                                    "Separate multiple file names with spaces.")
    export_parser.add_argument('-C', '--use-id2entry', action='store_true', help="Uses only the main database file")
    export_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="""Decrypts encrypted data during export. This option is used only
                                       if database encryption is enabled.""")
    export_parser.add_argument('-m', '--min-base64', action='store_true',
                               help="Sets minimal base-64 encoding")
    export_parser.add_argument('-N', '--no-seq-num', action='store_true',
                               help="Suppresses printing the sequence numbers")
    export_parser.add_argument('-r', '--replication', action='store_true',
                               help="Exports the data with information required to initialize a replica")
    export_parser.add_argument('-u', '--no-dump-uniq-id', action='store_true',
                               help="Omits exporting the unique ID")
    export_parser.add_argument('-U', '--not-folded', action='store_true',
                               help="Disables folding the output")
    export_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included")
    export_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded")
    export_parser.add_argument('--timeout', default=0, type=int,
                               help="Set a timeout to wait for the export task.  Default is 0 (no timeout)")

    #######################################################
    # Create a new backend database
    #######################################################
    create_parser = subcommands.add_parser('create', help='Create a backend database', formatter_class=CustomHelpFormatter)
    create_parser.set_defaults(func=backend_create)
    create_parser.add_argument('--parent-suffix', default=False,
                               help="Sets the parent suffix only if this backend is a sub-suffix")
    create_parser.add_argument('--suffix', required=True, help='Sets the database suffix DN')
    create_parser.add_argument('--be-name', required=True, help='Sets the database backend name"')
    create_parser.add_argument('--create-entries', action='store_true', help='Adds sample entries to the database')
    create_parser.add_argument('--create-suffix', action='store_true',
                               help="Creates the suffix object entry in the database. Only suffixes using the 'dc', 'o', 'ou', or 'cn' attributes are supported.")

    #######################################################
    # Delete backend
    #######################################################
    delete_parser = subcommands.add_parser('delete', help='Delete a backend database', formatter_class=CustomHelpFormatter)
    delete_parser.set_defaults(func=backend_delete)
    delete_parser.add_argument('be_name', help='The backend name or suffix')
    delete_parser.add_argument('--do-it', dest="ack",
                               help="Remove backend and its subsuffixes",
                               action='store_true', default=False)

    #######################################################
    # Get Suffix Tree (for use in web console)
    #######################################################
    get_tree_parser = subcommands.add_parser('get-tree', help='Display the suffix tree', formatter_class=CustomHelpFormatter)
    get_tree_parser.set_defaults(func=backend_get_tree)

    #######################################################
    # Run the db compaction task
    #######################################################
    compact_parser = subcommands.add_parser('compact-db', help='Compact the database and the replication changelog', formatter_class=CustomHelpFormatter)
    compact_parser.set_defaults(func=backend_compact)
    compact_parser.add_argument('--only-changelog', action='store_true', help='Compacts only the replication change log')
    compact_parser.add_argument('--timeout', default=0, type=int,
                                help="Set a timeout to wait for the compaction task.  Default is 0 (no timeout)")

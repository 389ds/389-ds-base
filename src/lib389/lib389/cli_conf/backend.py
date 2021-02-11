# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.backend import Backend, Backends, DatabaseConfig
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
from lib389._constants import *
from lib389.cli_base import (
    _format_status,
    _generic_get,
    _generic_get_dn,
    _get_arg,
    _warn,
    )
import json
import ldap

arg_to_attr = {
        'lookthroughlimit': 'nsslapd-lookthroughlimit',
        'mode': 'nsslapd-mode',
        'idlistscanlimit': 'nsslapd-idlistscanlimit',
        'directory': 'nsslapd-directory',
        'dbcachesize': 'nsslapd-dbcachesize',
        'logdirectory': 'nsslapd-db-logdirectory',
        'durable_txn': 'nsslapd-db-durable-transaction',
        'txn_wait': 'nsslapd-db-transaction-wait',
        'checkpoint_interval': 'nsslapd-db-checkpoint-interval',
        'compactdb_interval': 'nsslapd-db-compactdb-interval',
        'txn_batch_val': 'nsslapd-db-transaction-batch-val',
        'txn_batch_min': 'nsslapd-db-transaction-batch-min-wait',
        'txn_batch_max': 'nsslapd-db-transaction-batch-max-wait',
        'logbufsize': 'nsslapd-db-logbuf-size',
        'locks': 'nsslapd-db-locks',
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
    found = False
    be_insts = MANY(inst).list()
    for be in be_insts:
        cn = ensure_str(be.get_attr_val('cn')).lower()
        suffix = ensure_str(be.get_attr_val('nsslapd-suffix')).lower()
        del_be_name = be_name.lower()
        if cn == del_be_name or suffix == del_be_name:
            dn = be.dn
            found = True
            break
    if found:
        return dn


def _get_backend(inst, name):
    be_insts = Backends(inst).list()
    for be in be_insts:
        be_suffix = ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower()
        cn = ensure_str(be.get_attr_val_utf8_l('cn')).lower()
        if be_suffix == name.lower() or cn == name.lower():
            return be

    raise ValueError('Could not find backend suffix: {}'.format(name))


def _get_index(inst, bename, attr):
    be_insts = Backends(inst).list()
    for be in be_insts:
        be_suffix = ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix'))
        cn = ensure_str(be.get_attr_val_utf8_l('cn')).lower()
        if be_suffix == bename.lower() or cn == bename.lower():
            for index in be.get_indexes().list():
                idx_name = index.get_attr_val_utf8_l('cn').lower()
                if idx_name == attr.lower():
                    return index
    raise ValueError('Could not find index: {}'.format(attr))


def backend_list(inst, basedn, log, args):
    be_list = []
    be_insts = MANY(inst).list()
    for be in be_insts:
        suffix = be.get_attr_val_utf8_l('nsslapd-suffix').lower()
        be_name = be.get_attr_val_utf8_l('cn').lower()
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
    _generic_get(inst, basedn, log.getChild('backend_get'), MANY, rdn, args)


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
    if warn and args.json is False:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))

    be = _get_backend(inst, args.be_name)
    _recursively_del_backends(be)
    be.delete()

    log.info("The database, and any sub-suffixes, were sucessfully deleted")


def backend_import(inst, basedn, log, args):
    log = log.getChild('backend_import')
    dn = _search_backend_dn(inst, args.be_name)
    if dn is None:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))

    mc = SINGULAR(inst, dn)
    task = mc.import_ldif(ldifs=args.ldifs, chunk_size=args.chunks_size, encrypted=args.encrypted,
                          gen_uniq_id=args.gen_uniq_id, only_core=args.only_core, include_suffixes=args.include_suffixes,
                          exclude_suffixes=args.exclude_suffixes)
    task.wait(timeout=None)
    result = task.get_exit_code()
    warning = task.get_task_warn()

    if task.is_complete() and result == 0:
        if warning is None or (warning == 0):
            log.info("The import task has finished successfully")
        else:
            log.info("The import task has finished successfully, with warning code {}, check the logs for more detail".format(warning))
    else:
        raise ValueError("Import task failed\n-------------------------\n{}".format(ensure_str(task.get_task_log())))


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
    task.wait(timeout=None)
    result = task.get_exit_code()

    if task.is_complete() and result == 0:
        log.info("The export task has finished successfully")
    else:
        raise ValueError("Export task failed\n-------------------------\n{}".format(ensure_str(task.get_task_log())))


def is_db_link(inst, rdn):
    links = ChainingLinks(inst).list()
    for link in links:
        cn = link.get_attr_val_utf8('cn').lower()
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
        be_suffix = ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower()
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
        raise ValueError("You can not set the backend to be both enabled and disabled at the same time")

    # Update backend
    be = _get_backend(inst, args.be_name)
    if args.enable_readonly:
        be.set('nsslapd-readonly', 'on')
    if args.disable_readonly:
        be.set('nsslapd-readonly', 'off')
    if args.add_referral:
        be.add('nsslapd-referral', args.add_referral)
    if args.del_referral:
        be.remove('nsslapd-referral', args.del_referral)
    if args.cache_size:
        be.set('nsslapd-cachesize', args.cache_size)
    if args.cache_memsize:
        be.set('nsslapd-cachememsize', args.cache_memsize)
    if args.dncache_memsize:
        be.set('nsslapd-dncachememsize', args.dncache_memsize)
    if args.require_index:
        be.set('nsslapd-require-index', 'on')
    if args.ignore_index:
        be.set('nsslapd-require-index', 'off')
    if args.enable:
        be.enable()
    if args.disable:
        be.disable()
    log.info("The backend configuration was successfully updated")


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
                    log.info()

    if args.json:
        log.info(json.dumps({"type": "list", "items": results}, indent=4))


def backend_get_vlv(inst, basedn, log, args):
    results = []
    be = _get_backend(inst, args.be_name)
    vlvs = be.get_vlv_searches()
    for vlv in vlvs:
        vlv_name = vlv.get_attr_val_utf8_l('cn').lower()
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


def create_parser(subparsers):
    backend_parser = subparsers.add_parser('backend', help="Manage database suffixes and backends")
    subcommands = backend_parser.add_subparsers(help="action")

    #####################################################
    # Suffix parser
    #####################################################
    suffix_parser = subcommands.add_parser('suffix', help="Manage a backend suffix")
    suffix_subcommands = suffix_parser.add_subparsers(help="action")

    # List backends/suffixes
    list_parser = suffix_subcommands.add_parser('list', help="List current active backends and suffixes")
    list_parser.set_defaults(func=backend_list)
    list_parser.add_argument('--suffix', action='store_true', help='Just display the suffix, and not the backend name')
    list_parser.add_argument('--skip-subsuffixes', action='store_true', help='Skip over sub-suffixes')

    # Get backend
    get_parser = suffix_subcommands.add_parser('get', help='Get the suffix entry')
    get_parser.set_defaults(func=backend_get)
    get_parser.add_argument('selector', nargs='?', help='The backend to search for')

    # Get the DN of a backend
    get_dn_parser = suffix_subcommands.add_parser('get-dn', help='get_dn')
    get_dn_parser.set_defaults(func=backend_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The backend dn to get')

    # Get subsuffixes
    get_subsuffix_parser = suffix_subcommands.add_parser('get-sub-suffixes', help='Get the sub-suffixes of this backend')
    get_subsuffix_parser.set_defaults(func=backend_get_subsuffixes)
    get_subsuffix_parser.add_argument('--suffix', action='store_true', help='Just display the suffix, and not the backend name')
    get_subsuffix_parser.add_argument('be_name', help='The backend name or suffix to search for sub-suffixes')

    # Set the backend/suffix configuration
    set_backend_parser = suffix_subcommands.add_parser('set', help='Set configuration settings for a single backend')
    set_backend_parser.set_defaults(func=backend_set)
    set_backend_parser.add_argument('--enable-readonly', action='store_true', help='Set backend database to be read-only')
    set_backend_parser.add_argument('--disable-readonly', action='store_true', help='Disable read-only mode for backend database')
    set_backend_parser.add_argument('--require-index', action='store_true', help='Only allow indexed searches')
    set_backend_parser.add_argument('--ignore-index', action='store_true', help='Allow all searches even if they are unindexed')
    set_backend_parser.add_argument('--add-referral', help='Add a LDAP referral to the backend')
    set_backend_parser.add_argument('--del-referral', help='Remove a LDAP referral to the backend')
    set_backend_parser.add_argument('--enable', action='store_true', help='Enable the backend database')
    set_backend_parser.add_argument('--disable', action='store_true', help='Disable the backend database')
    set_backend_parser.add_argument('--cache-size', help='The maximum number of entries to keep in the entry cache')
    set_backend_parser.add_argument('--cache-memsize', help='The maximum size in bytes that the entry cache can grow to')
    set_backend_parser.add_argument('--dncache-memsize', help='The maximum size in bytes that the DN cache can grow to')
    set_backend_parser.add_argument('be_name', help='The backend name or suffix to delete')

    #########################################
    # Index parser
    #########################################
    index_parser = subcommands.add_parser('index', help="Manage backend indexes")
    index_subcommands = index_parser.add_subparsers(help="action")

    # Create index
    add_index_parser = index_subcommands.add_parser('add', help='Set configuration settings for a single backend')
    add_index_parser.set_defaults(func=backend_add_index)
    add_index_parser.add_argument('--index-type', required=True, action='append', help='An indexing type: eq, sub, pres, or approximate')
    add_index_parser.add_argument('--matching-rule', action='append', help='Matching rule for the index')
    add_index_parser.add_argument('--reindex', action='store_true', help='After adding new index, reindex the database')
    add_index_parser.add_argument('--attr', required=True, help='The index attribute\'s name')
    add_index_parser.add_argument('be_name', help='The backend name or suffix to delete')

    # Edit index
    edit_index_parser = index_subcommands.add_parser('set', help='Edit an index entry')
    edit_index_parser.set_defaults(func=backend_set_index)
    edit_index_parser.add_argument('--attr', required=True, help='The index name to edit')
    edit_index_parser.add_argument('--add-type', action='append', help='An index type to add to the index: eq, sub, pres, or approx')
    edit_index_parser.add_argument('--del-type', action='append', help='An index type to remove from the index: eq, sub, pres, or approx')
    edit_index_parser.add_argument('--add-mr', action='append', help='A matching-rule to add to the index')
    edit_index_parser.add_argument('--del-mr', action='append', help='A matching-rule to remove from the index')
    edit_index_parser.add_argument('--reindex', action='store_true', help='After editing index, reindex the database')
    edit_index_parser.add_argument('be_name', help='The backend name or suffix to edit an index from')

    # Get index
    get_index_parser = index_subcommands.add_parser('get', help='Get an index entry')
    get_index_parser.set_defaults(func=backend_get_index)
    get_index_parser.add_argument('--attr', required=True, action='append', help='The index name to get')
    get_index_parser.add_argument('be_name', help='The backend name or suffix to get the index from')

    # list indexes
    list_index_parser = index_subcommands.add_parser('list', help='Set configuration settings for a single backend')
    list_index_parser.set_defaults(func=backend_list_index)
    list_index_parser.add_argument('--just-names', action='store_true', help='Return a list of just the attribute names for a backend')
    list_index_parser.add_argument('be_name', help='The backend name or suffix to list indexes from')

    # Delete index
    del_index_parser = index_subcommands.add_parser('delete', help='Set configuration settings for a single backend')
    del_index_parser.set_defaults(func=backend_del_index)
    del_index_parser.add_argument('--attr', action='append', help='The index attribute\'s name')
    del_index_parser.add_argument('be_name', help='The backend name or suffix to delete')

    # reindex index
    reindex_parser = index_subcommands.add_parser('reindex', help='Reindex the database (for a single index or all indexes')
    reindex_parser.set_defaults(func=backend_reindex)
    reindex_parser.add_argument('--attr', action='append', help='The index attribute\'s name to reindex.  Skip this argument to reindex all attributes')
    reindex_parser.add_argument('--wait', action='store_true', help='Wait for the index task to complete and report the status')
    reindex_parser.add_argument('be_name', help='The backend name or suffix to reindex')

    #############################################
    # VLV parser
    #############################################
    vlv_parser = subcommands.add_parser('vlv-index', help="Manage VLV searches and indexes")
    vlv_subcommands = vlv_parser.add_subparsers(help="action")

    # List VLV Searches
    list_vlv_search_parser = vlv_subcommands.add_parser('list', help='List VLV search and index entries')
    list_vlv_search_parser.set_defaults(func=backend_list_vlv)
    list_vlv_search_parser.add_argument('--just-names', action='store_true', help='List just the names of the VLV search entries')
    list_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Get VLV search entry and indexes
    get_vlv_search_parser = vlv_subcommands.add_parser('get', help='Get a VLV search & index')
    get_vlv_search_parser.set_defaults(func=backend_get_vlv)
    get_vlv_search_parser.add_argument('--name', help='Get the VLV search entry and its index entries')
    get_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Create VLV Search
    add_vlv_search_parser = vlv_subcommands.add_parser('add-search', help='Add a VLV search entry.  The search entry is the parent entry '
                                                                             'of the VLV index entries, and it specifies the search params that '
                                                                             'are used to match entries for those indexes.')
    add_vlv_search_parser.set_defaults(func=backend_create_vlv)
    add_vlv_search_parser.add_argument('--name', required=True, help='Name of the VLV search entry')
    add_vlv_search_parser.add_argument('--search-base', required=True, help='The VLV search base')
    add_vlv_search_parser.add_argument('--search-scope', required=True, help='The VLV search scope: 0 (base search), 1 (one-level search), or 2 (subtree search)')
    add_vlv_search_parser.add_argument('--search-filter', required=True, help='The VLV search filter')
    add_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Edit vlv search
    edit_vlv_search_parser = vlv_subcommands.add_parser('edit-search', help='Edit a VLV search & index')
    edit_vlv_search_parser.set_defaults(func=backend_edit_vlv)
    edit_vlv_search_parser.add_argument('--name', required=True, help='Name of the VLV index')
    edit_vlv_search_parser.add_argument('--search-base', help='The VLV search base')
    edit_vlv_search_parser.add_argument('--search-scope', help='The VLV search scope: 0 (base search), 1 (one-level search), or 2 (subtree search)')
    edit_vlv_search_parser.add_argument('--search-filter', help='The VLV search filter')
    edit_vlv_search_parser.add_argument('--reindex', action='store_true', help='Reindex all the VLV database indexes')
    edit_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Delete vlv search(and index)
    del_vlv_search_parser = vlv_subcommands.add_parser('del-search', help='Delete VLV search & index')
    del_vlv_search_parser.set_defaults(func=backend_del_vlv)
    del_vlv_search_parser.add_argument('--name', required=True, help='Name of the VLV search index')
    del_vlv_search_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Create VLV Index
    add_vlv_index_parser = vlv_subcommands.add_parser('add-index', help='Create a VLV index under a VLV search entry(parent entry).  '
                                                                        'The VLV index just specifies the attributes to sort')
    add_vlv_index_parser.set_defaults(func=backend_create_vlv_index)
    add_vlv_index_parser.add_argument('--parent-name', required=True, help='Name, or "cn" attribute value, of the parent VLV search entry')
    add_vlv_index_parser.add_argument('--index-name', required=True, help='Name of the new VLV index')
    add_vlv_index_parser.add_argument('--sort', required=True, help='A space separated list of attributes to sort for this VLV index')
    add_vlv_index_parser.add_argument('--index-it', action='store_true', help='Create the database index for this VLV index definition')
    add_vlv_index_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Delete VLV Index
    del_vlv_index_parser = vlv_subcommands.add_parser('del-index', help='Delete a VLV index under a VLV search entry(parent entry).')
    del_vlv_index_parser.set_defaults(func=backend_delete_vlv_index)
    del_vlv_index_parser.add_argument('--parent-name', required=True, help='Name, or "cn" attribute value, of the parent VLV search entry')
    del_vlv_index_parser.add_argument('--index-name', help='Name of the VLV index to delete')
    del_vlv_index_parser.add_argument('--sort', help='Delete a VLV index that has this vlvsort value')
    del_vlv_index_parser.add_argument('be_name', help='The backend name of the VLV index')

    # Reindex VLV
    reindex_vlv_parser = vlv_subcommands.add_parser('reindex', help='Index/reindex the VLV database index')
    reindex_vlv_parser.set_defaults(func=backend_reindex_vlv)
    reindex_vlv_parser.add_argument('--index-name', help='Name of the VLV Index entry to reindex.  If not set, all indexes are reindexed')
    reindex_vlv_parser.add_argument('--parent-name', required=True, help='Name, or "cn" attribute value, of the parent VLV search entry')
    reindex_vlv_parser.add_argument('be_name', help='The backend name of the VLV index')

    ############################################
    # Encrypted Attributes
    ############################################
    attr_encrypt_parser = subcommands.add_parser('attr-encrypt', help='Encrypted attribute options')
    attr_encrypt_parser.set_defaults(func=backend_attr_encrypt)
    attr_encrypt_parser.add_argument('--list', action='store_true', help='List all the encrypted attributes for this backend')
    attr_encrypt_parser.add_argument('--just-names', action='store_true', help='List just the names of the encrypted attributes (used with --list)')
    attr_encrypt_parser.add_argument('--add-attr', action='append', help='Add an attribute to be encrypted')
    attr_encrypt_parser.add_argument('--del-attr', action='append', help='Remove an attribute from being encrypted')
    attr_encrypt_parser.add_argument('be_name', help='The backend name or suffix to to reindex')

    ############################################
    # Global DB Config
    ############################################
    db_parser = subcommands.add_parser('config', help="Manage the global database configuration settings")
    db_subcommands = db_parser.add_subparsers(help="action")

    # Get the global database configuration
    get_db_config_parser = db_subcommands.add_parser('get', help='Get the global database configuration')
    get_db_config_parser.set_defaults(func=db_config_get)

    # Update the global database configuration
    set_db_config_parser = db_subcommands.add_parser('set', help='Set the global database configuration')
    set_db_config_parser.set_defaults(func=db_config_set)
    set_db_config_parser.add_argument('--lookthroughlimit', help='specifies the maximum number of entries that the Directory '
                                                                 'Server will check when examining candidate entries in response to a search request')
    set_db_config_parser.add_argument('--mode', help='Specifies the permissions used for newly created index files')
    set_db_config_parser.add_argument('--idlistscanlimit', help='Specifies the number of entry IDs that are searched during a search operation')
    set_db_config_parser.add_argument('--directory', help='Specifies absolute path to database instance')
    set_db_config_parser.add_argument('--dbcachesize', help='Specifies the database index cache size, in bytes.')
    set_db_config_parser.add_argument('--logdirectory', help='Specifies the path to the directory that contains the database transaction logs')
    set_db_config_parser.add_argument('--durable-txn', help='Sets whether database transaction log entries are immediately written to the disk.')
    set_db_config_parser.add_argument('--txn-wait', help='Sets whether the server should should wait if there are no db locks available')
    set_db_config_parser.add_argument('--checkpoint-interval', help='Sets the amount of time in seconds after which the Directory Server sends a '
                                                                    'checkpoint entry to the database transaction log')
    set_db_config_parser.add_argument('--compactdb-interval', help='Sets the interval in seconds when the database is compacted')
    set_db_config_parser.add_argument('--txn-batch-val', help='Specifies how many transactions will be batched before being committed')
    set_db_config_parser.add_argument('--txn-batch-min', help='Controls when transactions should be flushed earliest, independently of '
                                                              'the batch count (only works when txn-batch-val is set)')
    set_db_config_parser.add_argument('--txn-batch-max', help='Controls when transactions should be flushed latest, independently of '
                                                              'the batch count (only works when txn-batch-val is set)')
    set_db_config_parser.add_argument('--logbufsize', help='Specifies the transaction log information buffer size')
    set_db_config_parser.add_argument('--locks', help='Sets the maximum number of database locks')
    set_db_config_parser.add_argument('--import-cache-autosize', help='Set to "on" or "off" to automatically set the size of the import '
                                                                       'cache to be used during the the import process of LDIF files')
    set_db_config_parser.add_argument('--cache-autosize', help='Sets the percentage of free memory that is used in total for the database '
                                                               'and entry cache.  Set to "0" to disable this feature.')
    set_db_config_parser.add_argument('--cache-autosize-split', help='Sets the percentage of RAM that is used for the database cache. The '
                                                                     'remaining percentage is used for the entry cache')
    set_db_config_parser.add_argument('--import-cachesize', help='Sets the size, in bytes, of the database cache used in the import process.')
    set_db_config_parser.add_argument('--exclude-from-export', help='List of attributes to not include during database export operations')
    set_db_config_parser.add_argument('--pagedlookthroughlimit', help='Specifies the maximum number of entries that the Directory Server '
                                                                      'will check when examining candidate entries for a search which uses '
                                                                      'the simple paged results control')
    set_db_config_parser.add_argument('--pagedidlistscanlimit', help='Specifies the number of entry IDs that are searched, specifically, '
                                                                     'for a search operation using the simple paged results control.')
    set_db_config_parser.add_argument('--rangelookthroughlimit', help='Specifies the maximum number of entries that the Directory Server '
                                                                      'will check when examining candidate entries in response to a '
                                                                      'range search request.')
    set_db_config_parser.add_argument('--backend-opt-level', help='WARNING this parameter can trigger experimental code to improve write '
                                                                  'performance.  Valid values are: 0, 1, 2, or 4')
    set_db_config_parser.add_argument('--deadlock-policy', help='Adjusts the backend database deadlock policy (Advanced setting)')
    set_db_config_parser.add_argument('--db-home-directory', help='Sets the directory for the database mmapped files (Advanced setting)')

    #######################################################
    # Database & Suffix Monitor
    #######################################################
    get_monitor_parser = subcommands.add_parser('monitor', help="Get the global database monitor information")
    get_monitor_parser.set_defaults(func=get_monitor)
    get_monitor_parser.add_argument('--suffix', help='Get just the suffix monitor entry')

    #######################################################
    # Import LDIF
    #######################################################
    import_parser = subcommands.add_parser('import', help="Do an online import of the suffix")
    import_parser.set_defaults(func=backend_import)
    import_parser.add_argument('be_name', nargs='?',
                               help='The backend name or the root suffix where to import')
    import_parser.add_argument('ldifs', nargs='*',
                               help="Specifies the filename of the input LDIF files."
                                    "When multiple files are imported, they are imported in the order"
                                    "they are specified on the command line.")
    import_parser.add_argument('-c', '--chunks-size', type=int,
                               help="The number of chunks to have during the import operation.")
    import_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="Decrypts encrypted data during export. This option is used only"
                                    "if database encryption is enabled.")
    import_parser.add_argument('-g', '--gen-uniq-id',
                               help="Generate a unique id. Type none for no unique ID to be generated"
                                    "and deterministic for the generated unique ID to be name-based."
                                    "By default, a time-based unique ID is generated."
                                    "When using the deterministic generation to have a name-based unique ID,"
                                    "it is also possible to specify the namespace for the server to use."
                                    "namespaceId is a string of characters"
                                    "in the format 00-xxxxxxxx-xxxxxxxx-xxxxxxxx-xxxxxxxx.")
    import_parser.add_argument('-O', '--only-core', action='store_true',
                               help="Requests that only the core database is created without attribute indexes.")
    import_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included.")
    import_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded.")

    #######################################################
    # Export LDIF
    #######################################################
    export_parser = subcommands.add_parser('export', help='Do an online export of the suffix')
    export_parser.set_defaults(func=backend_export)
    export_parser.add_argument('be_names', nargs='+',
                               help="The backend names or the root suffixes from where to export.")
    export_parser.add_argument('-l', '--ldif',
                               help="Gives the filename of the output LDIF file."
                                    "If more than one are specified, use a space as a separator")
    export_parser.add_argument('-C', '--use-id2entry', action='store_true', help="Uses only the main database file.")
    export_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="""Decrypts encrypted data during export. This option is used only
                                       if database encryption is enabled.""")
    export_parser.add_argument('-m', '--min-base64', action='store_true',
                               help="Sets minimal base-64 encoding.")
    export_parser.add_argument('-N', '--no-seq-num', action='store_true',
                               help="Enables you to suppress printing the sequence number.")
    export_parser.add_argument('-r', '--replication', action='store_true',
                               help="Exports the information required to initialize a replica when the LDIF is imported")
    export_parser.add_argument('-u', '--no-dump-uniq-id', action='store_true',
                               help="Requests that the unique ID is not exported.")
    export_parser.add_argument('-U', '--not-folded', action='store_true',
                               help="Requests that the output LDIF is not folded.")
    export_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included.")
    export_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded.")

    #######################################################
    # Create a new backend database
    #######################################################
    create_parser = subcommands.add_parser('create', help='Create a backend database')
    create_parser.set_defaults(func=backend_create)
    create_parser.add_argument('--parent-suffix', default=False,
                               help="Sets the parent suffix only if this backend is a sub-suffix")
    create_parser.add_argument('--suffix', required=True, help='The database suffix DN, for example "dc=example,dc=com"')
    create_parser.add_argument('--be-name', required=True, help='The database backend name, for example "userroot"')
    create_parser.add_argument('--create-entries', action='store_true', help='Create sample entries in the database')
    create_parser.add_argument('--create-suffix', action='store_true',
        help="Create the suffix object entry in the database.  Only suffixes using the attributes 'dc', 'o', 'ou', or 'cn' are supported in this feature")

    #######################################################
    # Delete backend
    #######################################################
    delete_parser = subcommands.add_parser('delete', help='Delete a backend database')
    delete_parser.set_defaults(func=backend_delete)
    delete_parser.add_argument('be_name', help='The backend name or suffix to delete')

    #######################################################
    # Get Suffix Tree (for use in web console)
    #######################################################
    get_tree_parser = subcommands.add_parser('get-tree', help='Get a representation of the suffix tree')
    get_tree_parser.set_defaults(func=backend_get_tree)

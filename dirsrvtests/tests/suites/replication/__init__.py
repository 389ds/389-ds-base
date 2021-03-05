"""
   :Requirement: 389-ds-base: Replication
"""
import time
import ldap
from lib389._constants import DEFAULT_SUFFIX


def get_repl_entries(topo, entry_name, attr_list):
    """Get a list of test entries from all suppliers"""

    entries_list = []

    time.sleep(10)

    for inst in topo.all_insts.values():
        entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "uid={}".format(entry_name), attr_list)
        entries_list += entries

    return entries_list


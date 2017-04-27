#!/usr/bin/python

# Display stats about pblock usage.

def parse(filepath):
    results = []
    flat_results = []
    # Read the file
    with open(filepath, 'r') as f:
        # For each line
        for l in f.readlines():
            pbc = {}
            pbs = set()
            # Trim the line.
            ls = l.strip()
            for access in ls.split(','):
                if len(access.strip()) == 0:
                    continue
                k, count = access.split(':')
                pbc[k] = int(count)
                pbs.add(k)
            if (pbc not in results):
                results.append(pbc)
            if (pbs not in flat_results):
                flat_results.append(pbs)
            #print(pbc)
    return results, flat_results

def determine_pblock_groups(flat_results):
    # This function will take the flattened results, and calculate a unique
    # set of pblock parameter combinations that may exist.
    # Make a set of all possible access values.
    results = {}
    access_values = set()
    for pbs in flat_results:
        access_values.update(pbs)

    print('all possible values %s' % access_values)
    # For each access value
    for av in access_values:
        results[av] = 0
        for s in flat_results:
            if av in s:
                results[av] += 1
    # Count the number of sets that contain it.
    # Sort by most popular access values.
    # Take the first pblock.
    #     take it's first value.
    #     make a new set
    for r in sorted(results, key=results.get, reverse=True):
        print('pop: av %s count %s' % (r, results[r]))

    # Find things that are subsets, and eliminate them.
    ss_flat_results = []
    for pbs_a in flat_results:
        subset = False
        for pbs_b in flat_results:
            if pbs_a.issubset(pbs_b) and pbs_a != pbs_b:
                subset = True
        if subset is False:
            ss_flat_results.append(pbs_a)

    for pbs in sorted(ss_flat_results, key=len):
        print('pbs -> %s' % sorted(pbs))
    print('== unique filtered pblocks %s ==' % len(ss_flat_results))

    # We build some sets of attrs we expect in certain things
    e_plugin_set = set(['13'])
    e_search_set = set(['195', '113'])
    e_modrdn_set = set(['100', '103', '101'])
    e_backend_plugin_set = set(['213', '202', '214'])
    e_op_set = set(['1001', '47'])
    e_task_set = set(['178', '181', '182', '192'])
    e_dse_set = set(['282', '283', '281', '289'])

    # Now, we can group these by plugin callback, and searches.
    # Check for plugin_enabled
    task_set = set()
    backend_plugin_set = set()
    plugin_set = set()
    search_set = set()
    modrdn_set = set()
    urp_set = set()
    op_set = set()
    dse_set = set()

    for pbs in ss_flat_results:
        # 3 is plugin, and 130 backend.
        if e_search_set & pbs:
            search_set.update(pbs)
        elif e_modrdn_set & pbs:
            modrdn_set.update(pbs)
        elif e_op_set & pbs:
            op_set.update(pbs)
        elif e_task_set & pbs:
            task_set.update(pbs)
        elif e_backend_plugin_set & pbs:
            backend_plugin_set.update(pbs)
        elif e_plugin_set & pbs:
            plugin_set.update(pbs)
        elif e_dse_set & pbs:
            dse_set.update(pbs)
        else:
            print('unclassified pb %s' % sorted(pbs))

    # Now, make each set unique to itself.

    excess_set = access_values - (task_set | backend_plugin_set | plugin_set | search_set | op_set | dse_set | modrdn_set )



    print('plugin_set: %s' % sorted(plugin_set))
    print('')
    print('backend_plugin_set: %s' % sorted(backend_plugin_set))
    print('')
    print('search_set: %s' % sorted(search_set))
    print('')
    print('modrdn_set: %s' % sorted(modrdn_set))
    print('')
    print('urp_set: %s' % sorted(urp_set))
    print('')
    print('task_set: %s' % sorted(task_set))
    print('')
    print('op_set: %s' % sorted(op_set))
    print('')
    print('dse_set: %s' % sorted(dse_set))
    print('')
    print('excess_set: %s' % sorted(excess_set))
    print('')

    print ('== frequency of overlap from %s unique pblocks ==' % (len(ss_flat_results)))
    for r in sorted(results, key=results.get, reverse=True):
        if r in overlap_set:
            print('pop: av %s count %s' % (r, results[r]))


def analyse():
    # Read the file
    results, flat_results = parse('/tmp/pblock_stats.csv')
    # Results is now a flat set of pblocks.
    for pbs in flat_results:
        print(pbs)
    determine_pblock_groups(flat_results)



if __name__ == '__main__':
    analyse()

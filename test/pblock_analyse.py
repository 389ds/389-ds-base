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

    #for pbs in sorted(ss_flat_results, key=len):
    #    print(pbs)

    # Now, we can group these by plugin callback, and searches.
    # Check for plugin_enabled
    plugin_set = set()
    search_set = set()
    other_set = set()
    for pbs in ss_flat_results:
        # 815 is plugin_enabled
        if '815' in pbs:
            plugin_set.update(pbs)
        elif '216' in pbs: # This is db_result_fn
            search_set.update(pbs)
        else:
            other_set.update(pbs)

    overlap_set = plugin_set & search_set

    print('plugin_set: %s' % plugin_set)
    print('search_set: %s' % search_set)
    print('other_set: %s' % other_set)
    print('overlap : %s' % (plugin_set & search_set))

    print ('== frequency of overlap from %s unique pblocks ==' % (len(flat_results)))
    for r in sorted(results, key=results.get, reverse=True):
        if r in overlap_set:
            print('pop: av %s count %s' % (r, results[r]))
    print ('== frequency of other from %s unique pblocks ==' % (len(flat_results)))
    for r in sorted(results, key=results.get, reverse=True):
        if r in other_set:
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

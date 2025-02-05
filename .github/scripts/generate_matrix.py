import os
import sys
import glob
import json

# If we have arguments passed to the script, use them as the test names to run
if len(sys.argv) > 1:
    suites = sys.argv[1:]
    valid_suites = []
    # Validate if the path is a valid file or directory with files
    for suite in suites:
        test_path = os.path.join("dirsrvtests/tests/suites/", suite)
        if os.path.exists(test_path) and not os.path.islink(test_path):
            if os.path.isfile(test_path) and test_path.endswith(".py"):
                valid_suites.append(suite)
            elif os.path.isdir(test_path):
                valid_suites.append(suite)
    suites = valid_suites

else:
    # Use tests from the source
    suites = next(os.walk('dirsrvtests/tests/suites/'))[1]

    # Filter out snmp as it is an empty directory:
    suites.remove('snmp')

    # Filter out webui because of broken tests
    suites.remove('webui')

    # Run each replication test module separately to speed things up
    suites.remove('replication')
    repl_tests = glob.glob('dirsrvtests/tests/suites/replication/*_test.py')
    suites += [repl_test.replace('dirsrvtests/tests/suites/', '') for repl_test in repl_tests]
    suites.sort()

suites_list = [{ "suite": suite} for suite in suites]
matrix = {"include": suites_list}

print(json.dumps(matrix))


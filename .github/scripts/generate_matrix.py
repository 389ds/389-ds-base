import os
import glob
import json

suites = next(os.walk('dirsrvtests/tests/suites/'))[1]

# Filter out snmp as it is an empty directory:
suites.remove('snmp')

# Run each replication test module separately to speed things up
suites.remove('replication')
repl_tests = glob.glob('dirsrvtests/tests/suites/replication/*_test.py')
suites += [repl_test.replace('dirsrvtests/tests/suites/', '') for repl_test in repl_tests]
suites.sort()

suites_list = [{ "suite": suite} for suite in suites]
matrix = {"include": suites_list}

print(json.dumps(matrix))
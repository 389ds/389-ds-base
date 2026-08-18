import os
import sys
import glob
import json

SUITES_PATH = "dirsrvtests/tests/suites"


def is_valid_suite_dir(suite):
    test_path = os.path.join(SUITES_PATH, suite)
    return (
        os.path.isdir(test_path)
        and not os.path.islink(test_path)
        and not suite.startswith(".")
        and suite != "__pycache__"
    )


def is_valid_suite_file(suite):
    test_path = os.path.join(SUITES_PATH, suite)
    return (
        os.path.isfile(test_path)
        and not os.path.islink(test_path)
        and suite.endswith(".py")
    )


# If we have arguments passed to the script, use them as the test names to run
if len(sys.argv) > 1:
    suites = sys.argv[1:]
    valid_suites = []
    # Validate if the path is a valid file or directory with files
    for suite in suites:
        if is_valid_suite_file(suite) or is_valid_suite_dir(suite):
            valid_suites.append(suite)
    suites = valid_suites

else:
    # Use tests from the source
    suites = [suite for suite in next(os.walk(SUITES_PATH))[1] if is_valid_suite_dir(suite)]

    # Run each replication test module separately to speed things up
    suites.remove('replication')
    repl_tests = glob.glob(os.path.join(SUITES_PATH, 'replication/*_test.py'))
    suites += [repl_test.replace(f'{SUITES_PATH}/', '') for repl_test in repl_tests]
    suites.sort()

suites_list = [{ "suite": suite} for suite in suites]
matrix = {"include": suites_list}

print(json.dumps(matrix))

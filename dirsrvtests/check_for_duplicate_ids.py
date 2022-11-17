# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import subprocess
import sys


def check_for_duplicates(path):
    """Check for duplicate id tokens in tests"""
    prefix = ":id:"
    cmd = ["grep", "-rhi", f"{prefix}", path]
    p = subprocess.run(cmd, check=True, stdout=subprocess.PIPE)
    ids = [x.replace(prefix, "").strip() for x in p.stdout.decode().splitlines()]
    return set([x for x in ids if ids.count(x) > 1])


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} path_to_tests")
        sys.exit(1)
    else:
        path = sys.argv[1]
        if os.path.exists(path):
            dups = check_for_duplicates(path)
            if len(dups) > 0:
                print("Found duplicate ids:")
                for dup in dups:
                    print(dup)
                sys.exit(1)
            else:
                print("No duplicates found")
                sys.exit(0)
        else:
            print(f"Path {path} doesn't exist, exiting...")
            sys.exit(1)


if __name__ == "__main__":
    main()


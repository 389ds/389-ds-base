#!/usr/bin/env python3
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import re
import sys
import subprocess
import argparse

def get_lib389_version():
    """Get hardcoded version from pyproject.toml"""
    try:
        with open('pyproject.toml', 'r') as f:
            content = f.read()
            version_match = re.search(r'version\s*=\s*"([^"]+)"', content)
            if version_match:
                version = version_match.group(1)
                return version
    except Exception as e:
        print(f"ERROR: Failed to read version from pyproject.toml: {e}")
    return None

def get_main_project_version():
    """Get version from the main 389-ds-base VERSION.sh file"""
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    version_sh_path = os.path.join(root_dir, 'VERSION.sh')

    if not os.path.exists(version_sh_path):
        print(f"ERROR: Main project VERSION.sh not found at {version_sh_path}")
        return None

    try:
        command = f"source {version_sh_path} && echo $RPM_VERSION"
        result = subprocess.run(['bash', '-c', command], capture_output=True, text=True)
        if result.returncode == 0:
            version = result.stdout.strip()
            return version
    except Exception as e:
        print(f"ERROR: Failed to get main project version: {e}")

    return None

def update_lib389_version(new_version):
    """Update the version in pyproject.toml"""
    try:
        with open('pyproject.toml', 'r') as f:
            content = f.read()

        # Replace hardcoded version with new version
        updated_content = re.sub(r'version\s*=\s*"[^"]+"', f'version = "{new_version}"', content)

        with open('pyproject.toml', 'w') as f:
            f.write(updated_content)

        print(f"SUCCESS: Updated lib389 version to {new_version} in pyproject.toml")
        return True
    except Exception as e:
        print(f"ERROR: Failed to update version in pyproject.toml: {e}")
        return False

def validate_versions(auto_update=False):
    """Validate that lib389 version matches the main project version"""
    lib389_version = get_lib389_version()
    main_version = get_main_project_version()

    if not lib389_version:
        print("ERROR: Failed to get lib389 version from pyproject.toml")
        return False

    if not main_version:
        print("ERROR: Failed to get main project version from VERSION.sh")
        return False

    if lib389_version != main_version:
        print(f"ERROR: Version mismatch detected!")
        print(f"Main project version: {main_version}")
        print(f"lib389 version:       {lib389_version}")

        if auto_update:
            return update_lib389_version(main_version)
        else:
            print("\nTo update the version in pyproject.toml, run:")
            print(f"  python3 validate_version.py --update")
            return False

    print(f"SUCCESS: Versions match! Version: {lib389_version}")
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Validate lib389 version against main project version')
    parser.add_argument('--update', action='store_true', help='Automatically update lib389 version to match main project version')
    args = parser.parse_args()

    if not validate_versions(args.update):
        sys.exit(1)
    sys.exit(0)

#!/usr/bin/python3
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
# PYTHON_ARGCOMPLETE_OK

import os
import sys
import subprocess
import time
import signal
import argparse
import argcomplete
import shutil
import json
import re
import logging
from typing import List, Dict, Tuple, Set

SPECFILE_COMMENT_LINE = 'Bundled cargo crates list'
START_LINE = f"##### {SPECFILE_COMMENT_LINE} - START #####\n"
END_LINE = f"##### {SPECFILE_COMMENT_LINE} - END #####\n"

IGNORED_RUST_PACKAGES: Set[str] = {"librslapd", "librnsslapd", "slapd", "slapi_r_plugin", "entryuuid", "entryuuid_syntax", "pwdchan"}
IGNORED_NPM_PACKAGES: Set[str] = {"389-console"}
PACKAGE_REGEX = re.compile(r"(.*)@(.*)")

parser = argparse.ArgumentParser(
    formatter_class=argparse.RawDescriptionHelpFormatter,
    description="""Add 'Provides: bundled(crate(foo)) = version' 'Provides: bundled(npm(bar)) = version' to a Fedora based specfile.
Additionally, add a helper comment with a comulated License metainfo which is based on Cargo.lock and Package-lock.json files content.
You need to have 'cargo install cargo-license' and 'dnf install npm' to be able to run this script.""")

parser.add_argument('-v', '--verbose',
                    help="Display verbose operation tracing during command execution",
                    action='store_const', const=logging.DEBUG, default=logging.WARNING)
parser.add_argument('-f', '--fix-it',
                    help="Don't comment out License: field",
                    action='store_true', default=False)
parser.add_argument('cargo_path',
                    help="The path to the directory with Cargo.lock file.")
parser.add_argument('npm_path',
                    help="The path to the directory with Package-lock.json file.")
parser.add_argument('spec_file',
                    help="The path to spec file that will be modified.")
parser.add_argument('--backup-specfile',
                    help="Make a backup of the downstream specfile.",
                    action='store_true', default=False)


# handle a control-c gracefully
def signal_handler(signal, frame):
    """Exits the script gracefully on SIGINT."""
    print('\n\nExiting...')
    sys.exit(0)


def backup_specfile(spec_file: str):
    """Creates a backup of the specfile with a timestamp."""
    try:
        time_now = time.strftime("%Y%m%d_%H%M%S")
        log.info(f"Backing up file {spec_file} to {spec_file}.{time_now}")
        shutil.copy2(spec_file, f"{spec_file}.{time_now}")
    except IOError as e:
        log.error(f"Failed to backup specfile: {e}")
        sys.exit(1)


def run_cmd(cmd):
    """Executes a command and returns its output."""
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    args = ' '.join(result.args)
    log.debug(f"CMD: {args} returned {result.returncode} STDOUT: {result.stdout} STDERR: {result.stderr}")
    return result.stdout


def process_rust_crates(output: str) -> Dict[str, Tuple[str, str]]:
    """Processes the output from cargo-license to extract crate information."""
    crates = json.loads(output)
    return {crate['name']: (enclose_if_contains_or(crate['license']), crate['version'])
            for crate in crates if crate['name'] not in IGNORED_RUST_PACKAGES}


def process_npm_packages(output: str) -> Dict[str, Tuple[str, str]]:
    """Processes the output from license-checker to extract npm package information."""
    packages = json.loads(output)
    processed_packages = {}
    for package, data in packages.items():
        package_name, package_version = PACKAGE_REGEX.match(package).groups()
        if package_name not in IGNORED_NPM_PACKAGES:
            npm_license = process_npm_license(data['licenses'])
            # Check if the package is 'pause-stream' and if the license is 'Apache2'
            # If so, replace it with 'Apache-2.0' to match the license in Upstream
            # It is a workaround till the pause-stream's fix is released
            if package_name == "pause-stream" and "Apache2" in npm_license:
                npm_license = npm_license.replace("Apache2", "Apache-2.0")
            # Check if the package is 'argparse' and if the license is 'Python-2.0'
            # If so, replace it with 'PSF-2.0' as sugested here:
            # https://gitlab.com/fedora/legal/fedora-license-data/-/issues/470
            # It is a workaround till the issue resolved
            if package_name == "argparse" and "Python-2.0" in npm_license:
                npm_license = npm_license.replace("Python-2.0", "PSF-2.0")
            processed_packages[package_name] = (npm_license, package_version)

    return processed_packages


def process_npm_license(license_data) -> str:
    """Formats the license data for npm packages."""
    npm_license = license_data if isinstance(license_data, str) else ' OR '.join(license_data)
    return enclose_if_contains_or(npm_license)


def enclose_if_contains_or(license_str: str) -> str:
    """Enclose the license string in parentheses if it contains 'OR'."""
    return f"({license_str})" if 'OR' in license_str and not license_str.startswith('(') else license_str


def build_provides_lines(rust_crates: Dict[str, Tuple[str, str]], npm_packages: Dict[str, Tuple[str, str]]) -> list[str]:
    """Builds lines to be added to the spec file for provided packages."""
    provides_lines = [f"Provides:  bundled(crate({crate})) = {version.replace('-', '_')}\n"
                      for crate, (_, version) in rust_crates.items()]
    provides_lines += [f"Provides:  bundled(npm({package})) = {version.replace('-', '_')}\n"
                       for package, (_, version) in npm_packages.items()]
    return provides_lines


def create_license_line(rust_crates: Dict[str, Tuple[str, str]], npm_packages: Dict[str, Tuple[str, str]]) -> str:
    """Creates a line for the spec file with combined license information."""
    licenses = {license for _, (license, _) in {**rust_crates, **npm_packages}.items() if license}
    return " AND ".join(sorted(licenses))


def replace_license(spec_file: str, license_string: str):
    """Replaces the license section in the spec file with a new license string and
    adds a comment for manual review and adjustment.
    """
    result = []
    with open(spec_file, "r") as file:
        contents = file.readlines()
        for line in contents:
            if line.startswith("License: "):
                if args.fix_it:
                    result.append(f"License:          GPL-3.0-or-later AND {license_string}\n")
                else:
                    result.append("# IMPORTANT - Check if it looks right. Additionally, "
                                  "compare with the original line. Then, remove this comment and # FIXME - part.\n")
                    result.append(f"# FIXME - License:          GPL-3.0-or-later AND {license_string}\n")
            else:
                result.append(line)
    with open(spec_file, "w") as file:
        file.writelines(result)
    log.info(f"Licenses are successfully updated - {spec_file}")


def clean_specfile(spec_file: str) -> bool:
    """Cleans up the spec file by removing the previous bundled package information.
    Returns a boolean indicating if the clean-up was successful.
    """
    result = []
    remove_lines = False
    cleaned = False
    with open(spec_file, "r") as file:
        contents = file.readlines()

    log.info(f"Remove '{SPECFILE_COMMENT_LINE}' content from {spec_file}")
    for line in contents:
        if line == START_LINE:
            remove_lines = True
            log.debug(f"Remove '{START_LINE}' from {spec_file}")
        elif line == END_LINE:
            remove_lines = False
            cleaned = True
            log.debug(f"Remove '{END_LINE}' from {spec_file}")
        elif not remove_lines:
            result.append(line)
        else:
            log.debug(f"Remove '{line}' from {spec_file}")

    with open(spec_file, "w") as file:
        file.writelines(result)
    return cleaned


def write_provides_bundled(provides_lines: List[str], spec_file: str, cleaned: bool):
    """Writes bundled package information to the spec file.
    Includes generated 'Provides' lines and marks the section for easy future modification.
    """
    # Find a line index where 'Provides' ends
    with open(spec_file, "r") as file:
        spec_file_lines = file.readlines()
    last_provides = -1
    for i in range(0, len(spec_file_lines)):
        if spec_file_lines[i].startswith("%description"):
            break
        if spec_file_lines[i].startswith("Provides:"):
            last_provides = i

    # Insert the generated 'Provides' to the specfile
    log.info(f"Add the fresh '{SPECFILE_COMMENT_LINE}' content to {spec_file}")
    i = last_provides + 2
    spec_file_lines.insert(i, START_LINE)
    for line in sorted(provides_lines):
        i = i + 1
        log.debug(f"Adding '{line[:-1]}' as a line {i} to buffer")
        spec_file_lines.insert(i, line)
    i = i + 1
    spec_file_lines.insert(i, END_LINE)

    # Insert an empty line if we haven't cleaned the old content
    # (as the old content already has an extra empty line that wasn't removed)
    if not cleaned:
        i = i + 1
        spec_file_lines.insert(i, "\n")

    log.debug(f"Commit the buffer to {spec_file}")
    with open(spec_file, "w") as file:
        file.writelines(spec_file_lines)


if __name__ == '__main__':
    args = parser.parse_args()
    logging.basicConfig(level=args.verbose)
    log = logging.getLogger("bundle-rust-npm")

    log.debug("389-ds-base Rust Crates and Node Modules to Bundled Downstream spec file tool")
    log.debug(f"Called with: {args}")

    if args.backup_specfile:
        backup_specfile(args.spec_file)

    if not os.path.isdir(args.cargo_path):
        log.error(f"Path {args.cargo_path} does not exist or is not a directory")
        sys.exit(1)
    if not os.path.isdir(args.npm_path):
        log.error(f"Path {args.npm_path} does not exist or is not a directory")
        sys.exit(1)

    if shutil.which("cargo-license") is None:
        log.error("cargo-license is not installed. Please install it with 'cargo install cargo-license' and try again.")
        sys.exit(1)
    if shutil.which("npm") is None:
        log.error("npm is not installed. Please install it with 'dnf install npm' and try again.")
        sys.exit(1)

    rust_output = run_cmd(["cargo", "license", "--json", "--current-dir", args.cargo_path])
    npm_output = run_cmd(["npx", "--yes", "license-checker", "--production", "--json", "--start", args.npm_path])

    if rust_output is None or npm_output is None:
        log.error("Failed to process dependencies. Ensure cargo-license and license-checker are installed and accessible. "
                  "Also, ensure that Cargo.lock and Package-lock.json files exist in the respective directories.")
        sys.exit(1)

    cleaned = clean_specfile(args.spec_file)

    rust_crates = process_rust_crates(rust_output)
    npm_packages = process_npm_packages(npm_output)
    provides_lines = build_provides_lines(rust_crates, npm_packages)

    write_provides_bundled(provides_lines, args.spec_file, cleaned)

    license_string = create_license_line(rust_crates, npm_packages)
    replace_license(args.spec_file, license_string)
    if not args.fix_it:
        print(f"Spec file {args.spec_file} is successfully modified! Please:")
        print("1. Open the spec file with your editor of choice")
        print("2. Make sure that 'Provides:' with bundled crates are correct")
        print("3. Follow the instructions for 'License:' field and remove the helper comments")


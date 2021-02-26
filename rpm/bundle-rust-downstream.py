#!/usr/bin/python3

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
# PYTHON_ARGCOMPLETE_OK

import os
import sys
import time
import signal
import argparse
import argcomplete
import shutil
import toml
from lib389.cli_base import setup_script_logger
from rust2rpm import licensing

SPECFILE_COMMENT_LINE = 'Bundled cargo crates list'
START_LINE = f"##### {SPECFILE_COMMENT_LINE} - START #####\n"
END_LINE = f"##### {SPECFILE_COMMENT_LINE} - END #####\n"


parser = argparse.ArgumentParser(
    formatter_class=argparse.RawDescriptionHelpFormatter,
    description="""Add 'Provides: bundled(crate(foo)) = version' to a Fedora based specfile. 
Additionally, add a helper comment with a comulated License metainfo which is based on Cargo.lock file content.""")

parser.add_argument('-v', '--verbose',
                    help="Display verbose operation tracing during command execution",
                    action='store_true', default=False)

parser.add_argument('cargo_lock_file',
                    help="The path to Cargo.lock file.")
parser.add_argument('spec_file',
                    help="The path to spec file that will be modified.")
parser.add_argument('vendor_dir',
                    help="The path to the vendor directory file that will be modified.")
parser.add_argument('--backup-specfile',
                    help="Make a backup of the downstream specfile.",
                    action='store_true', default=False)


# handle a control-c gracefully
def signal_handler(signal, frame):
    print('\n\nExiting...')
    sys.exit(0)


def get_license_list(vendor_dir):
    license_list = list()
    for root, _, files in os.walk(vendor_dir):
        for file in files:
            name = os.path.join(root, file)
            if os.path.isfile(name) and "Cargo.toml" in name:
                with open(name, "r") as file:
                    contents = file.read()
                data = toml.loads(contents)
                license, warning = licensing.translate_license_fedora(data["package"]["license"])

                # Normalise
                license = license.replace("/", " or ").replace(" / ", " or ")
                license = license.replace("Apache-2.0", "ASL 2.0")
                license = license.replace("WITH LLVM-exception", "with exceptions")
                if "or" in license or "and" in license:
                    license = f"({license})"
                if license == "(MIT or ASL 2.0)":
                    license = "(ASL 2.0 or MIT)"

                if license not in license_list:
                    if warning is not None:
                        # Ignore known warnings
                        if not warning.endswith("LLVM-exception!") and \
                        not warning.endswith("MIT/Apache-2.0!"):
                            print(f"{license}: {warning}")
                    license_list.append(license)
    return " and ".join(license_list)


def backup_specfile(spec_file):
    time_now = time.strftime("%Y%m%d_%H%M%S")
    log.info(f"Backing up file {spec_file} to {spec_file}.{time_now}")
    shutil.copy2(spec_file, f"{spec_file}.{time_now}")


def replace_license(spec_file, license_string):
    result = []
    with open(spec_file, "r") as file:
        contents = file.readlines()
        for line in contents:
            if line.startswith("License: "):
                result.append("# IMPORTANT - Check if it looks right. Additionally, "
                              "compare with the original line. Then, remove this comment and # FIX ME - part.\n")
                result.append(f"# FIX ME - License:          GPLv3+ and {license_string}\n")
            else:
                result.append(line)
    with open(spec_file, "w") as file:
        file.writelines(result)
    log.info(f"Licenses are successfully updated - {spec_file}")


def clean_specfile(spec_file):
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


def write_provides_bundled_crate(cargo_lock_file, spec_file, cleaned):
    # Generate 'Provides' out of cargo_lock_file
    with open(cargo_lock_file, "r") as file:
        contents = file.read()
    data = toml.loads(contents)
    provides_lines = []
    for package in data["package"]:
        provides_lines.append(f"Provides:  bundled(crate({package['name']})) = {package['version'].replace('-', '_')}\n")

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
    log = setup_script_logger('bundle-rust-downstream', args.verbose)

    log.debug("389-ds-base Rust Crates to Bundled Downstream Specfile tool")
    log.debug(f"Called with: {args}")

    if not os.path.exists(args.spec_file):
        log.info(f"File doesn't exists: {args.spec_file}")
        sys.exit(1)
    if not os.path.exists(args.cargo_lock_file):
        log.info(f"File doesn't exists: {args.cargo_lock_file}")
        sys.exit(1)

    if args.backup_specfile:
        backup_specfile(args.spec_file)

    cleaned = clean_specfile(args.spec_file)
    write_provides_bundled_crate(args.cargo_lock_file, args.spec_file, cleaned)
    license_string = get_license_list(args.vendor_dir)
    replace_license(args.spec_file, license_string)
    log.info(f"Specfile {args.spec_file} is successfully modified! Please:\n"
              "1. Open the specfile with your editor of choice\n"
              "2. Make sure that Provides with bundled crates are correct\n"
              "3. Follow the instructions for 'License:' field and remove the helper comments")


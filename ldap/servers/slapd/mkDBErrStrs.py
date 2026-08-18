#!/usr/bin/python3

import re
import argparse


def build_header(args):
    """Dynamically build the all the bdb errors codes and strings into a header
    file used by the server.
    """

    re_dberr = re.compile(r'^ *DBI_RC_')
    if args.output_dir is not None:
        new_header = f"{args.output_dir}/dberrstrs.h"
    else:
        new_header = "dberrstrs.h"

    with open(f"{args.dbi_dir}/dbimpl.h") as bdb_file:
        err_list = []
        line = bdb_file.readline()
        while line:
            mo = re_dberr.search(line)
            if mo is not None:
                # Get the error code and error string from lines like this:
                err_split = line.split('*', 2)
                if len(err_split) == 3:
                    err_code, err_str = err_split[1].split(',', 1)
                    err_list.append((err_code.strip(), err_str.strip()))
            line = bdb_file.readline()

        # Sort the dict
        sorted_errs = sorted(err_list, reverse=True)

        # Write new header file for slapd
        with open(new_header, 'w') as new_file:
            new_file.write("/* DO NOT EDIT: This is an automatically generated file by mkDBErrStrs.py */\n")
            for err_code, err_str in sorted_errs:
                new_file.write(f'{{{err_code},\t"{err_str}"}},\n')


def main():
    desc = ("""Dynamically build the bdb error codes""")

    bdb_parser = argparse.ArgumentParser(description=desc, allow_abbrev=True)
    bdb_parser.add_argument('-o', '--output', help='The output file location',
                            dest='output_dir', default=None)
    bdb_parser.add_argument('-i', '--include-dir',
                            help='The location of the dbimpl header file',
                            dest='dbi_dir', required=True)
    args = bdb_parser.parse_args()

    # Do it!
    build_header(args)


if __name__ == '__main__':
    main()

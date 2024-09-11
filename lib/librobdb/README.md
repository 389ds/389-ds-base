# DESCRIPTION

This project provides basic functions to walk/lookup Berkeley Database records.
It is derived from  [GitHub - rpm-software-management/rpm: The RPM package manager](https://github.com/rpm-software-management/rpm/) project.
It reuse a single file: https://github.com/rpm-software-management/rpm/blob/master/lib/backend/bdb_ro.cc

renamed as a C file, suppressed librpm adherences and adding back a simple
interface to be able to use the relevant functions.

# Build

make clean rpmbuild lint

# Example

See test/test.c (Using a 389ds entries database as example, It shows how to dump the database and look for records)

# Running tests

dnf install -y dist/RPMS/*/*.rpm

make test

# LICENSE

Same as lib part for rpm:   GPLv2 or alternatively LGPL   (See COPYING and COPYING.RPM for full details)

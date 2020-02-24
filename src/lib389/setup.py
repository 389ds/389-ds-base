#!/usr/bin/python3

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

#
# A setup.py file
#

from setuptools import setup, find_packages
from os import path
from build_manpages import build_manpages
from setuptools.command.build_py import build_py

here = path.abspath(path.dirname(__file__))

# fedora/rhel versioning or PEP440?; ATM semantic versioning
# with open(path.join(here, 'VERSION'), 'r') as version_file:
# version = version_file.read().strip()

version = "1.4.0.1"

with open(path.join(here, 'README.md'), 'r') as f:
    long_description = f.read()

setup(
    name='lib389',
    license='GPLv3+',
    version=version,
    description='A library for accessing and configuring the 389 Directory ' +
                'Server',
    long_description=long_description,
    url='http://www.port389.org/docs/389ds/FAQ/upstream-test-framework.html',

    author='Red Hat Inc., and William Brown',
    author_email='389-devel@lists.fedoraproject.org',

    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Topic :: Software Development :: Libraries',
        'Topic :: Software Development :: Quality Assurance',
        'Topic :: Software Development :: Testing'],

    keywords='389 directory server test configure',
    packages=find_packages(exclude=['tests*']),

    # find lib389/clitools -name ds\* -exec echo \''{}'\', \;
    data_files=[
        ('/usr/sbin/', [
            'cli/dsctl',
            'cli/dsconf',
            'cli/dscreate',
            'cli/dsidm',
            'cli/openldap_to_ds',
            ]),
        ('/usr/share/man/man8', [
            'man/dsctl.8',
            'man/dsconf.8',
            'man/dscreate.8',
            'man/dsidm.8',
            'man/openldap_to_ds.8',
            ]),
        ('/usr/libexec/dirsrv/', [
            'cli/dscontainer',
            ]),
    ],

    install_requires=[
        'pyasn1',
        'pyasn1-modules',
        'pytest',
        'python-dateutil',
        'six',
        'argcomplete',
        'argparse-manpage',
        'python-ldap',
        'setuptools',
        'distro',
        ],

    cmdclass={
        # Dynamically build man pages for cli tools
        'build_manpages': build_manpages.build_manpages,
        'build_py': build_manpages.get_build_py_cmd(build_py),
    }

)

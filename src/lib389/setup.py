#!/usr/bin/env python

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
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

here = path.abspath(path.dirname(__file__))

# fedora/rhel versioning or PEP440?; ATM semantic versioning
with open(path.join(here, 'VERSION'), 'r') as version_file:
    version = version_file.read().strip()

with open(path.join(here, 'README'), 'r') as f:
    long_description = f.read()

setup(
    name='lib389',
    license='GPLv3+',
    version=version,
    description='A library for accessing and configuring the 389 Directory ' +
                'Server',
    long_description=long_description,
    url='http://port389.org/wiki/Upstream_test_framework',

    author='Red Hat Inc.',
    author_email='389-devel@lists.fedoraproject.org',

    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: Python :: 2 :: Only',
        'Programming Language :: Python :: 2.7',
        'Topic :: Software Development :: Libraries',
        'Topic :: Software Development :: Quality Assurance',
        'Topic :: Software Development :: Testing'],

    keywords='389 directory server test configure',
    packages=find_packages(exclude=['tests*']),

    # find lib389/clitools -name ds\* -exec echo \''{}'\', \;
    data_files=[
        ('/usr/sbin/', [
            # 'lib389/clitools/ds_setup',
            'lib389/clitools/ds_aci_lint',
            'lib389/clitools/ds_list_instances',
            'lib389/clitools/ds_monitor_backend',
            'lib389/clitools/ds_monitor_server',
            'lib389/clitools/ds_schema_attributetype_list',
            'lib389/clitools/ds_schema_attributetype_query',
            'lib389/clitools/ds_start',
            'lib389/clitools/ds_stop',
            'lib389/clitools/ds_krb_create_keytab',
            'lib389/clitools/ds_krb_create_principal',
            'lib389/clitools/ds_krb_create_realm',
            'lib389/clitools/ds_krb_destroy_realm',
            'lib389/clitools/ds_backend_getattr',
            'lib389/clitools/ds_backend_setattr',
            'lib389/clitools/ds_backend_list',
            ]),
    ],

    install_requires=['python-ldap'],
)

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import subprocess

"""
This class will allow general usage of ldclt.

It's not meant to expose all the functions. Just use ldclt for that.

It's meant to expose general use cases for tests in 389.

Calling this on a production DS instance is likely a fast way to MESS THINGS UP.

"""


class Ldclt(object):
    def __init__(self, ds):
        self.ds = ds
        self.verbose = self.ds.verbose
        self.log = self.ds.log

    def create_users(self, subtree, min=1000, max=9999, template=None):
        """
        Creates users as user<min through max>. Password will be set to

        password<number>

        This will automatically work with the bind loadtest.
        """
        # Should we check max > min?
        # Create the template file given the data.
        if template is None:
            template = """
objectClass: top
objectclass: person
objectClass: organizationalPerson
objectClass: inetorgperson
objectClass: posixAccount
objectClass: shadowAccount
sn: user[A]
cn: user[A]
givenName: user[A]
description: description [A]
userPassword: user[A]
mail: user[A]@example.com
uidNumber: 1[A]
gidNumber: 2[A]
shadowMin: 0
shadowMax: 99999
shadowInactive: 30
shadowWarning: 7
homeDirectory: /home/user[A]
loginShell: /bin/false
"""
        with open('/tmp/ldclt_template_lib389.ldif', 'wb') as f:
            f.write(template)
        # call ldclt with the current rootdn and rootpass
        digits = len('%s' % max)

        cmd = [
            '%s/bin/ldclt-bin' % self.ds.prefix,
            '-h',
            self.ds.host,
            '-p',
            '%s' % self.ds.port,
            '-D',
            self.ds.binddn,
            '-w',
            self.ds.bindpw,
            '-b',
            subtree,
            '-e',
            'add,commoncounter',
            '-e',
            "object=/tmp/ldclt_template_lib389.ldif,rdn=uid:user[A=INCRNNOLOOP(%s;%s;%s)]" % (min, max, digits),
        ]
        result = None
        if self.verbose:
            self.log.info("ldclt begining user create ...")
            self.log.info(' '.join(cmd))
        try:
            result = subprocess.check_output(cmd)
        # If verbose, capture / log the output.
        except subprocess.CalledProcessError as e:
            print(' '.join(cmd))
            print(result)
            raise(e)
        if self.verbose:
            self.log.info(result)

    def bind_loadtest(self, subtree, min=1000, max=9999, rounds=3):
        # The bind users will be uid=userXXXX
        digits = len('%s' % max)
        cmd = [
            '%s/bin/ldclt-bin' % self.ds.prefix,
            '-h',
            self.ds.host,
            '-p',
            '%s' % self.ds.port,
            '-N',
            '%s' % rounds,
            '-D',
            'uid=user%s,%s' % ('X' * digits, subtree),
            '-w',
            'user%s' % ('X' * digits),
            '-e',
            "randombinddn,randombinddnlow=%s,randombinddnhigh=%s" % (min, max),
            '-e',
            'bindonly',
        ]
        result = None
        if self.verbose:
            self.log.info("ldclt loadtest ...")
            self.log.info(' '.join(cmd))
        try:
            result = subprocess.check_output(cmd)
        # If verbose, capture / log the output.
        except subprocess.CalledProcessError as e:
            print(' '.join(cmd))
            print(result)
            raise(e)
        if self.verbose:
            self.log.info(result)

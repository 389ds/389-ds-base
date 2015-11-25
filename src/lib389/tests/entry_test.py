# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
from lib389 import Entry
import lib389
import pytest

import logging
logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)


class TestEntry(object):
    """A properly initialized Entry:
        - accepts well-formed or empty dn and tuples;
        - refuses empty dn
    """
    def test_init_empty(self):
        e = Entry('')
        assert not e.dn

    def test_init_with_str(self):
        e = Entry('o=pippo')
        assert e.dn == 'o=pippo'

    def test_init_badstr(self):
        # This should not be allowed
        with pytest.raises(ValueError):
            e = Entry('no equal sign here')

    def test_init_with_tuple(self):
        expected = 'pippo'
        given = 'o=pippo'
        t = (given, {
             'o': [expected],
             'objectclass': ['organization', 'top']
             })
        e = Entry(t)
        assert e.dn == given
        assert expected in e.o

    def test_update(self):
        expected = 'pluto minnie'
        given = {'cn': expected}
        t = ('o=pippo', {
             'o': ['pippo'],
             'objectclass': ['organization', 'top']
             })

        e = Entry(t)
        e.update(given)
        assert e.cn == expected, "Bad cn: %s, expected: %s" % (e.cn, expected)

    def test_update_complex(self):
        # compare two entries created with different methods
        nsuffix, replid, replicatype = ("dc=example,dc=com", 5,
                                        lib389.REPLICA_RDWR_TYPE)
        binddnlist, legacy = ['uid=pippo, cn=config'], 'off'
        dn = "dc=example,dc=com"
        entry = Entry(dn)
        entry.setValues(
            'objectclass', "top", "nsds5replica", "extensibleobject")
        entry.setValues('cn', "replica")
        entry.setValues('nsds5replicaroot', nsuffix)
        entry.setValues('nsds5replicaid', str(replid))
        entry.setValues('nsds5replicatype', str(replicatype))
        entry.setValues('nsds5flags', "1")
        entry.setValues('nsds5replicabinddn', binddnlist)
        entry.setValues('nsds5replicalegacyconsumer', legacy)

        uentry = Entry((
            dn, {'objectclass': ["top", "nsds5replica", "extensibleobject"],
                 'cn': ["replica"]})
        )
        log.debug("Entry created with dict:", uentry)
        # Entry.update *replaces*, so be careful with multi-valued attrs
        uentry.update({
            'nsds5replicaroot': nsuffix,
            'nsds5replicaid': str(replid),
            'nsds5replicatype': str(replicatype),
            'nsds5flags': '1',
            'nsds5replicabinddn': binddnlist,
            'nsds5replicalegacyconsumer': legacy
        })
        uentry_s, entry_s = list(map(str, (uentry, entry)))
        assert uentry_s == entry_s, "Mismatching entries [%r] vs [%r]" % (
            uentry, entry)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

import logging

logging.basicConfig(level=logging.DEBUG)

DN_RMANAGER = 'uid=rmanager,cn=config'

auth = {'host': 'localhost',
        'port': 22389,
        'binddn': 'cn=directory manager',
        'bindpw': 'password'}


class MockDirSrv(object):
    host = 'localhost'
    port = 22389
    sslport = 0

    def __str__(self):
        if self.sslport:
            return 'ldaps://%s:%s' % (self.host, self.sslport)
        else:
            return 'ldap://%s:%s' % (self.host, self.port)


def expect(entry, name, value):
    assert entry, "Bad entry %r " % entry
    assert entry.getValue(name) == value, \
        ("Bad value for entry %s. Expected %r vs %r" %
         (entry, entry.getValue(name), value))


def entry_equals(e1, e2):
    """compare using str()"""
    return str(e1) == str(e2)


def dfilter(my_dict, keys):
    """Filter a dict in a 2.4-compatible way"""
    return dict([(k, v) for k, v in my_dict.items() if k in keys])

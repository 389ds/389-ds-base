import logging
import pytest
import os
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)
ALL_FILTERS = []


# Parameterized filters to test
AND_FILTERS = [("(&(uid=uid1)(sn=last1)(givenname=first1))", 1),
               ("(&(uid=uid1)(&(sn=last1)(givenname=first1)))", 1),
               ("(&(uid=uid1)(&(&(sn=last1))(&(givenname=first1))))", 1),
               ("(&(uid=*)(sn=last3)(givenname=*))", 1),
               ("(&(uid=*)(&(sn=last3)(givenname=*)))", 1),
               ("(&(uid=uid5)(&(&(sn=*))(&(givenname=*))))", 1),
               ("(&(objectclass=*)(uid=*)(sn=last*))", 5),
               ("(&(objectclass=*)(uid=*)(sn=last1))", 1)]

OR_FILTERS = [("(|(uid=uid1)(sn=last1)(givenname=first1))", 1),
              ("(|(uid=uid1)(|(sn=last1)(givenname=first1)))", 1),
              ("(|(uid=uid1)(|(|(sn=last1))(|(givenname=first1))))", 1),
              ("(|(objectclass=*)(sn=last1)(|(givenname=first1)))", 18),
              ("(|(&(objectclass=*)(sn=last1))(|(givenname=first1)))", 1),
              ("(|(&(objectclass=*)(sn=last))(|(givenname=first1)))", 1)]

NOT_FILTERS = [("(&(uid=uid1)(!(cn=NULL)))", 1),
               ("(&(!(cn=NULL))(uid=uid1))", 1),
               ("(&(uid=*)(&(!(uid=1))(!(givenname=first1))))", 5)]

MIX_FILTERS = [("(&(|(uid=uid1)(uid=NULL))(sn=last1))", 1),
               ("(&(|(uid=uid1)(uid=NULL))(!(sn=NULL)))", 1),
               ("(&(|(uid=uid1)(sn=last2))(givenname=first1))", 1),
               ("(|(&(uid=uid1)(!(uid=NULL)))(sn=last2))", 2),
               ("(|(&(uid=uid1)(uid=NULL))(sn=last2))", 1),
               ("(&(uid=uid5)(sn=*)(cn=*)(givenname=*)(uid=u*)(sn=la*)" +
                "(cn=full*)(givenname=f*)(uid>=u)(!(givenname=NULL)))", 1),
               ("(|(&(objectclass=*)(sn=last))(&(givenname=first1)))", 1)]

ZERO_AND_FILTERS = [("(&(uid=uid1)(sn=last1)(givenname=NULL))", 0),
                   ("(&(uid=uid1)(&(sn=last1)(givenname=NULL)))", 0),
                   ("(&(uid=uid1)(&(&(sn=last1))(&(givenname=NULL))))", 0),
                   ("(&(uid=uid1)(&(&(sn=last1))(&(givenname=NULL)(sn=*)))(|(sn=NULL)))", 0),
                   ("(&(uid=uid1)(&(&(sn=last*))(&(givenname=first*)))(&(sn=NULL)))", 0)]

ZERO_OR_FILTERS = [("(|(uid=NULL)(sn=NULL)(givenname=NULL))", 0),
                  ("(|(uid=NULL)(|(sn=NULL)(givenname=NULL)))", 0),
                  ("(|(uid=NULL)(|(|(sn=NULL))(|(givenname=NULL))))", 0)]

RANGE_FILTERS = [("(uid>=uid3)", 3),
                 ("(&(uid=*)(uid>=uid3))", 3),
                 ("(|(uid>=uid3)(uid<=uid5))", 6),
                 ("(&(uid>=uid3)(uid<=uid5))", 3),
                 ("(|(&(uid>=uid3)(uid<=uid5))(uid=*))", 6)]

LONG_FILTERS = [("(|(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)(uid=*)" +
                 "(uid=*))", 6)]


# Combine all the filters
ALL_FILTERS += AND_FILTERS
ALL_FILTERS += OR_FILTERS
ALL_FILTERS += NOT_FILTERS
ALL_FILTERS += MIX_FILTERS
ALL_FILTERS += ZERO_AND_FILTERS
ALL_FILTERS += ZERO_OR_FILTERS
ALL_FILTERS += LONG_FILTERS
ALL_FILTERS += RANGE_FILTERS


@pytest.fixture(scope="module")
def setup(topo, request):
    """Add teset users
    """

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(1, 6):
        users.create(properties={
            'uid': 'uid%s' % i,
            'cn': 'full%s' % i,
            'sn': 'last%s' % i,
            'givenname': 'first%s' % i,
            'uidNumber': '%s' % i,
            'gidNumber': '%s' % i,
            'homeDirectory': '/home/user%s' % i
        })


@pytest.mark.parametrize("myfilter, expected_results", ALL_FILTERS)
def test_filters(topo, setup, myfilter, expected_results):
    """Test various complex search filters and verify they are returning the
    expected number of entries

    :id: ee9ead27-5f63-4aed-844d-c39b99138c8d
    :parametrized: yes
    :setup: standalone
    :steps:
        1. Issue search
        2. Check the number of returned entries against the expected number
    :expectedresults:
        1. Search succeeds
        2. The number of returned entries matches the expected number
    """

    log.info("Testing filter \"{}\"...".format(myfilter))
    try:
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
        if len(entries) != expected_results:
            log.fatal("Search filter \"{}\") returned {} entries, but we expected {}".format(
                myfilter, len(entries), expected_results))
            assert False
    except ldap.LDAPError as e:
        log.fatal("Search filter \"{}\") generated ldap error: {}".format(myfilter, str(e)))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


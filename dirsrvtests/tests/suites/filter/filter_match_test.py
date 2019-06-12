# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
Test the matching rules feature .
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st
from lib389.cos import CosTemplates
from lib389.schema import Schema

import ldap

pytestmark = pytest.mark.tier1


ATTR = ["( 2.16.840.1.113730.3.1.999999.0 NAME 'attroctetStringMatch' "
        "DESC 'for testing matching rules' EQUALITY octetStringMatch "
        "ORDERING octetStringOrderingMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.40 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.1 NAME 'attrbitStringMatch' DESC "
        "'for testing matching rules' EQUALITY bitStringMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.6 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.2 NAME 'attrcaseExactIA5Match' "
        "DESC 'for testing matching rules' EQUALITY caseExactIA5Match "
        "SUBSTR caseExactIA5SubstringsMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.3 NAME 'attrcaseExactMatch' DESC "
        "'for testing matching rules' EQUALITY caseExactMatch ORDERING "
        "caseExactOrderingMatch SUBSTR caseExactSubstringsMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.15 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.4 NAME 'attrgeneralizedTimeMatch' DESC "
        "'for testing matching rules' EQUALITY generalizedTimeMatch ORDERING "
        "generalizedTimeOrderingMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.24 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.5 NAME 'attrbooleanMatch' DESC "
        "'for testing matching rules' EQUALITY booleanMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.7 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.6 NAME 'attrcaseIgnoreIA5Match' DESC "
        "'for testing matching rules' EQUALITY caseIgnoreIA5Match SUBSTR "
        "caseIgnoreIA5SubstringsMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.7 NAME 'attrcaseIgnoreMatch' DESC "
        "'for testing matching rules' EQUALITY caseIgnoreMatch ORDERING "
        "caseIgnoreOrderingMatch SUBSTR caseIgnoreSubstringsMatch "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.8 NAME 'attrcaseIgnoreListMatch' DESC "
        "'for testing matching rules' EQUALITY caseIgnoreListMatch SUBSTR "
        "caseIgnoreListSubstringsMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.41 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.9 NAME 'attrobjectIdentifierMatch' DESC "
        "'for testing matching rules' EQUALITY objectIdentifierMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.38 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.10 NAME 'attrdistinguishedNameMatch' DESC "
        "'for testing matching rules' EQUALITY distinguishedNameMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.12 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.11 NAME 'attrintegerMatch' DESC "
        "'for testing matching rules' EQUALITY integerMatch ORDERING "
        "integerOrderingMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.12 NAME 'attruniqueMemberMatch' DESC "
        "'for testing matching rules' EQUALITY uniqueMemberMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.34 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.13 NAME 'attrnumericStringMatch' DESC "
        "'for testing matching rules' EQUALITY numericStringMatch ORDERING "
        "numericStringOrderingMatch SUBSTR numericStringSubstringsMatch "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.36 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.14 NAME 'attrtelephoneNumberMatch' DESC "
        "'for testing matching rules' EQUALITY telephoneNumberMatch SUBSTR "
        "telephoneNumberSubstringsMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.50 "
        "X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.15 NAME 'attrdirectoryStringFirstComponentMatch' "
        "DESC 'for testing matching rules' EQUALITY directoryStringFirstComponentMatch "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.16 NAME 'attrobjectIdentifierFirstComponentMatch' "
        "DESC 'for testing matching rules' EQUALITY objectIdentifierFirstComponentMatch "
        "SYNTAX 1.3.6.1.4.1.1466.115.121.1.38 X-ORIGIN 'matching rule tests' )",
        "( 2.16.840.1.113730.3.1.999999.17 NAME 'attrintegerFirstComponentMatch' "
        "DESC 'for testing matching rules' EQUALITY integerFirstComponentMatch SYNTAX "
        "1.3.6.1.4.1.1466.115.121.1.27 X-ORIGIN 'matching rule tests' )"]

TESTED_MATCHING_RULES = ["bitStringMatch", "caseExactIA5Match", "caseExactMatch",
                         "caseExactOrderingMatch", "caseExactSubstringsMatch",
                         "caseExactIA5SubstringsMatch", "generalizedTimeMatch",
                         "generalizedTimeOrderingMatch", "booleanMatch", "caseIgnoreIA5Match",
                         "caseIgnoreIA5SubstringsMatch", "caseIgnoreMatch",
                         "caseIgnoreOrderingMatch", "caseIgnoreSubstringsMatch",
                         "caseIgnoreListMatch", "caseIgnoreListSubstringsMatch",
                         "objectIdentifierMatch", "directoryStringFirstComponentMatch",
                         "objectIdentifierFirstComponentMatch", "distinguishedNameMatch",
                         "integerMatch", "integerOrderingMatch", "integerFirstComponentMatch",
                         "uniqueMemberMatch", "numericStringMatch", "numericStringOrderingMatch",
                         "numericStringSubstringsMatch", "telephoneNumberMatch",
                         "telephoneNumberSubstringsMatch", "octetStringMatch",
                         "octetStringOrderingMatch"]


MATCHING_RULES = [('addentrybitStringMatch', 'attrbitStringMatch',
                   ["'0010'B", "'0011'B", "'0100'B", "'0101'B", "'0110'B"],
                   ["'0001'B", "'0001'B", "'0010'B", "'0010'B", "'0011'B",
                    "'0011'B", "'0100'B", "'0100'B", "'0101'B",
                    "'0101'B", "'0110'B", "'0110'B"]),
                  ('addentrycaseExactIA5Match', 'attrcaseExactIA5Match',
                   ['sPrain', 'spRain', 'sprAin', 'spraIn', 'sprain'],
                   ['Sprain', 'Sprain', 'Sprain', 'Sprain', 'SpRain',
                    'SpRain', 'SprAin', 'SprAin', 'SpraIn', 'SpraIn',
                    'Sprain', 'Sprain']),
                  ('addentrycaseExactMatch', 'attrcaseExactMatch',
                   ['ÇéliNé Ändrè', 'Çéliné ÄndrÈ', 'Çéliné Ändrè', 'çÉliné Ändrè'],
                   ['ÇélIné Ändrè', 'ÇélIné Ändrè', 'ÇéliNé Ändrè', 'ÇéliNé Ändrè',
                    'Çéliné ÄndrÈ', 'Çéliné ÄndrÈ', 'Çéliné Ändrè', 'Çéliné Ändrè',
                    'çÉliné Ändrè', 'çÉliné Ändrè']),
                  ('addentrygeneralizedTimeMatch', 'attrgeneralizedTimeMatch',
                   ['20100218171301Z', '20100218171302Z', '20100218171303Z',
                    '20100218171304Z', '20100218171305Z'],
                   ['20100218171300Z', '20100218171300Z', '20100218171301Z',
                    '20100218171301Z', '20100218171302Z', '20100218171302Z',
                    '20100218171303Z', '20100218171303Z', '20100218171304Z',
                    '20100218171304Z', '20100218171305Z', '20100218171305Z']),
                  ('addentrybooleanMatch', 'attrbooleanMatch',
                   ['FALSE'],
                   ['TRUE', 'TRUE', 'FALSE', 'FALSE']),
                  ('addentrycaseIgnoreIA5Match', 'attrcaseIgnoreIA5Match',
                   ['sprain2', 'sprain3', 'sprain4', 'sprain5', 'sprain6'],
                   ['sprain1', 'sprain1', 'sprain2', 'sprain2', 'sprain3',
                    'sprain3', 'sprain4', 'sprain4', 'sprain5', 'sprain5',
                    'sprain6', 'sprain6']),
                  ('addentrycaseIgnoreMatch', 'attrcaseIgnoreMatch',
                   ['ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                    'ÇélIné Ändrè5', 'ÇélIné Ändrè6'],
                   ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2',
                    'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè3',
                    'ÇélIné Ändrè4', 'ÇélIné Ändrè4', 'ÇélIné Ändrè5',
                    'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']),
                  ('addentrycaseIgnoreListMatch', 'attrcaseIgnoreListMatch',
                   ['foo2$bar', 'foo3$bar', 'foo4$bar', 'foo5$bar', 'foo6$bar'],
                   ['foo1$bar', 'foo1$bar', 'foo2$bar', 'foo2$bar', 'foo3$bar',
                    'foo3$bar', 'foo4$bar', 'foo4$bar', 'foo5$bar', 'foo5$bar',
                    'foo6$bar', 'foo6$bar']),
                  ('addentryobjectIdentifierMatch', 'attrobjectIdentifierMatch',
                   ['1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.26',
                    '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.41',
                    '1.3.6.1.4.1.1466.115.121.1.6'],
                   ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                    '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                    '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                    '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                    '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                    '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']),
                  ('addentrydirectoryStringFirstComponentMatch',
                   'attrdirectoryStringFirstComponentMatch',
                   ['ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè5',
                    'ÇélIné Ändrè6'],
                   ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè2',
                    'ÇélIné Ändrè3', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè4',
                    'ÇélIné Ändrè5', 'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']),
                  ('addentryobjectIdentifierFirstComponentMatch',
                   'attrobjectIdentifierFirstComponentMatch',
                   ['1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.26',
                    '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.41',
                    '1.3.6.1.4.1.1466.115.121.1.6'],
                   ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                    '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                    '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                    '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                    '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                    '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']),
                  ('addentrydistinguishedNameMatch', 'attrdistinguishedNameMatch',
                   ['cn=foo2,cn=bar', 'cn=foo3,cn=bar', 'cn=foo4,cn=bar',
                    'cn=foo5,cn=bar', 'cn=foo6,cn=bar'],
                   ['cn=foo1,cn=bar', 'cn=foo1,cn=bar', 'cn=foo2,cn=bar',
                    'cn=foo2,cn=bar', 'cn=foo3,cn=bar', 'cn=foo3,cn=bar',
                    'cn=foo4,cn=bar', 'cn=foo4,cn=bar', 'cn=foo5,cn=bar',
                    'cn=foo5,cn=bar', 'cn=foo6,cn=bar', 'cn=foo6,cn=bar']),
                  ('addentryintegerMatch', 'attrintegerMatch',
                   ['-1', '0', '1', '2', '3'],
                   ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']),
                  ('addentryintegerFirstComponentMatch', 'attrintegerFirstComponentMatch',
                   ['-1', '0', '1', '2', '3'],
                   ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']),
                  ('addentryuniqueMemberMatch', 'attruniqueMemberMatch',
                   ["cn=foo2,cn=bar#'0010'B", "cn=foo3,cn=bar#'0011'B",
                    "cn=foo4,cn=bar#'0100'B", "cn=foo5,cn=bar#'0101'B",
                    "cn=foo6,cn=bar#'0110'B"],
                   ["cn=foo1,cn=bar#'0001'B", "cn=foo1,cn=bar#'0001'B",
                    "cn=foo2,cn=bar#'0010'B", "cn=foo2,cn=bar#'0010'B",
                    "cn=foo3,cn=bar#'0011'B", "cn=foo3,cn=bar#'0011'B",
                    "cn=foo4,cn=bar#'0100'B", "cn=foo4,cn=bar#'0100'B",
                    "cn=foo5,cn=bar#'0101'B", "cn=foo5,cn=bar#'0101'B",
                    "cn=foo6,cn=bar#'0110'B", "cn=foo6,cn=bar#'0110'B"]),
                  ('addentrynumericStringMatch', 'attrnumericStringMatch',
                   ['00002', '00003', '00004', '00005', '00006'],
                   ['00001', '00001', '00002', '00002', '00003', '00003', '00004',
                    '00004', '00005', '00005', '00006', '00006']),
                  ('addentrytelephoneNumberMatch', 'attrtelephoneNumberMatch',
                   ['+1 408 555 5625', '+1 408 555 6201', '+1 408 555 8585',
                    '+1 408 555 9187', '+1 408 555 9423'],
                   ['+1 408 555 4798', '+1 408 555 4798', '+1 408 555 5625',
                    '+1 408 555 5625', '+1 408 555 6201', '+1 408 555 6201',
                    '+1 408 555 8585', '+1 408 555 8585', '+1 408 555 9187',
                    '+1 408 555 9187', '+1 408 555 9423', '+1 408 555 9423']),
                  ('addentryoctetStringMatch', 'attroctetStringMatch',
                   ['AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=', 'AAAAAAAAAAAAAAQ=',
                    'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY='],
                   ['AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAI=',
                    'AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=', 'AAAAAAAAAAAAAAM=',
                    'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAU=',
                    'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY=', 'AAAAAAAAAAAAAAY='])]


def test_matching_rules(topology_st):
    """Test matching rules.
        :id: 8cb6e62a-8cfc-11e9-be9a-8c16451d917b
        :setup: Standalone
        :steps:
            1. Search for matching rule.
            2. Matching rule should be there in schema.
        :expected results:
            1. Pass
            2. Pass
    """
    matchingrules = Schema(topology_st.standalone).get_matchingrules()
    assert matchingrules
    rules = set(matchingrule.names for matchingrule in matchingrules)
    rules1 = [role[0] for role in rules if len(role) != 0]
    for rule in TESTED_MATCHING_RULES:
        assert rule in rules1


def test_add_attribute_types(topology_st):
    """Test add attribute types to schema
        :id: 84d6dece-8cfc-11e9-89a3-8c16451d917b
        :setup: Standalone
        :steps:
            1. Add new attribute types to schema.
        :expected results:
            1. Pass
    """
    for attribute in ATTR:
        Schema(topology_st.standalone).add('attributetypes', attribute)


@pytest.mark.parametrize("cn_cn, attr, positive, negative", MATCHING_RULES)
def test_valid_invalid_attributes(topology_st, cn_cn, attr, positive, negative):
    """Test valid and invalid values of attributes
        :id: 7ec19eca-8cfc-11e9-a0df-8c16451d917b
        :setup: Standalone
        :steps:
            1. Create entry with an attribute that uses that matching rule
            2. Delete existing entry
            3. Create entry with an attribute that uses that matching rule providing duplicate
            values that are duplicates according to the equality matching rule.
        :expected results:
            1. Pass
            2. Pass
            3. Fail
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    cos.create(properties={'cn': cn_cn,
                           attr: positive})
    for entry in cos.list():
        entry.delete()
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        cos.create(properties={'cn': cn_cn,
                               attr: negative})


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

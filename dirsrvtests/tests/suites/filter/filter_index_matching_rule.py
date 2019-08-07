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
from lib389.index import Indexes

import ldap

pytestmark = pytest.mark.tier1


TESTED_MATCHING_RULES = ["bitStringMatch",
                         "caseExactIA5Match",
                         "caseExactMatch",
                         "caseExactOrderingMatch",
                         "caseExactSubstringsMatch",
                         "caseExactIA5SubstringsMatch",
                         "generalizedTimeMatch",
                         "generalizedTimeOrderingMatch",
                         "booleanMatch",
                         "caseIgnoreIA5Match",
                         "caseIgnoreIA5SubstringsMatch",
                         "caseIgnoreMatch",
                         "caseIgnoreOrderingMatch",
                         "caseIgnoreSubstringsMatch",
                         "caseIgnoreListMatch",
                         "caseIgnoreListSubstringsMatch",
                         "objectIdentifierMatch",
                         "directoryStringFirstComponentMatch",
                         "objectIdentifierFirstComponentMatch",
                         "distinguishedNameMatch",
                         "integerMatch",
                         "integerOrderingMatch",
                         "integerFirstComponentMatch",
                         "uniqueMemberMatch",
                         "numericStringMatch",
                         "numericStringOrderingMatch",
                         "numericStringSubstringsMatch",
                         "telephoneNumberMatch",
                         "telephoneNumberSubstringsMatch",
                         "octetStringMatch",
                         "octetStringOrderingMatch"]


LIST_CN_INDEX = [('attroctetStringMatch', ['pres', 'eq']),
                 ('attrbitStringMatch', ['pres', 'eq']),
                 ('attrcaseExactIA5Match', ['pres', 'eq', 'sub']),
                 ('attrcaseExactMatch', ['pres', 'eq', 'sub']),
                 ('attrgeneralizedTimeMatch', ['pres', 'eq']),
                 ('attrbooleanMatch', ['pres', 'eq']),
                 ('attrcaseIgnoreIA5Match', ['pres', 'eq', 'sub']),
                 ('attrcaseIgnoreMatch', ['pres', 'eq', 'sub']),
                 ('attrcaseIgnoreListMatch', ['pres', 'eq', 'sub']),
                 ('attrobjectIdentifierMatch', ['pres', 'eq']),
                 ('attrdistinguishedNameMatch', ['pres', 'eq']),
                 ('attrintegerMatch', ['pres', 'eq']),
                 ('attruniqueMemberMatch', ['pres', 'eq']),
                 ('attrnumericStringMatch', ['pres', 'eq', 'sub']),
                 ('attrtelephoneNumberMatch', ['pres', 'eq', 'sub']),
                 ('attrdirectoryStringFirstComponentMatch', ['pres', 'eq']),
                 ('attrobjectIdentifierFirstComponentMatch', ['pres', 'eq']),
                 ('attrintegerFirstComponentMatch', ['pres', 'eq'])]


LIST_ATTR_INDEX = [
    {'attr': 'attrbitStringMatch',
     'positive': ["'0010'B", "'0011'B", "'0100'B", "'0101'B", "'0110'B"],
     'negative': ["'0001'B", "'0001'B", "'0010'B", "'0010'B", "'0011'B",
                  "'0011'B", "'0100'B", "'0100'B", "'0101'B", "'0101'B",
                  "'0110'B", "'0110'B"]},
    {'attr': 'attrcaseExactIA5Match',
     'positive': ['sPrain', 'spRain', 'sprAin', 'spraIn', 'sprain'],
     'negative': ['Sprain', 'Sprain', 'sPrain', 'sPrain', 'spRain',
                  'spRain', 'sprAin', 'sprAin', 'spraIn', 'spraIn',
                  'sprain', 'sprain']},
    {'attr': 'attrcaseExactMatch',
     'positive': ['ÇéliNé Ändrè', 'Çéliné ÄndrÈ', 'Çéliné Ändrè', 'çÉliné Ändrè'],
     'negative': ['ÇélIné Ändrè', 'ÇélIné Ändrè', 'ÇéliNé Ändrè', 'ÇéliNé Ändrè',
                  'Çéliné ÄndrÈ', 'Çéliné ÄndrÈ', 'Çéliné Ändrè', 'Çéliné Ändrè',
                  'çÉliné Ändrè', 'çÉliné Ändrè']},
    {'attr': 'attrgeneralizedTimeMatch',
     'positive': ['20100218171301Z', '20100218171302Z', '20100218171303Z',
                  '20100218171304Z', '20100218171305Z'],
     'negative': ['20100218171300Z', '20100218171300Z', '20100218171301Z',
                  '20100218171301Z', '20100218171302Z', '20100218171302Z',
                  '20100218171303Z', '20100218171303Z', '20100218171304Z',
                  '20100218171304Z', '20100218171305Z', '20100218171305Z']},
    {'attr': 'attrbooleanMatch',
     'positive': ['FALSE'],
     'negative': ['TRUE', 'TRUE', 'FALSE', 'FALSE']},
    {'attr': 'attrcaseIgnoreIA5Match',
     'positive': ['sprain2', 'sprain3', 'sprain4', 'sprain5', 'sprain6'],
     'negative': ['sprain1', 'sprain1', 'sprain2', 'sprain2', 'sprain3',
                  'sprain3', 'sprain4', 'sprain4', 'sprain5', 'sprain5',
                  'sprain6', 'sprain6']},
    {'attr': 'attrcaseIgnoreMatch',
     'positive': ['ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2',
                  'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè3',
                  'ÇélIné Ändrè4', 'ÇélIné Ändrè4', 'ÇélIné Ändrè5',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']},
    {'attr': 'attrcaseIgnoreListMatch',
     'positive': ['foo2$bar', 'foo3$bar', 'foo4$bar', 'foo5$bar', 'foo6$bar'],
     'negative': ['foo1$bar', 'foo1$bar', 'foo2$bar', 'foo2$bar', 'foo3$bar',
                  'foo3$bar', 'foo4$bar', 'foo4$bar', 'foo5$bar', 'foo5$bar',
                  'foo6$bar', 'foo6$bar']},
    {'attr': 'attrobjectIdentifierMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                  '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdirectoryStringFirstComponentMatch',
     'positive': ['ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè5',
                  'ÇélIné Ändrè6'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè2',
                  'ÇélIné Ändrè3', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']},
    {'attr': 'attrobjectIdentifierFirstComponentMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                  '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdistinguishedNameMatch',
     'positive': ['cn=foo2,cn=bar', 'cn=foo3,cn=bar', 'cn=foo4,cn=bar', 'cn=foo5,cn=bar',
                  'cn=foo6,cn=bar'],
     'negative': ['cn=foo1,cn=bar', 'cn=foo1,cn=bar', 'cn=foo2,cn=bar', 'cn=foo2,cn=bar',
                  'cn=foo3,cn=bar', 'cn=foo3,cn=bar', 'cn=foo4,cn=bar', 'cn=foo4,cn=bar',
                  'cn=foo5,cn=bar', 'cn=foo5,cn=bar', 'cn=foo6,cn=bar', 'cn=foo6,cn=bar']},
    {'attr': 'attrintegerMatch',
     'positive': ['-1', '0', '1', '2', '3'],
     'negative': ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']},
    {'attr': 'attrintegerFirstComponentMatch',
     'positive': ['-1', '0', '1', '2', '3'],
     'negative': ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']},
    {'attr': 'attruniqueMemberMatch',
     'positive': ["cn=foo2,cn=bar#'0010'B", "cn=foo3,cn=bar#'0011'B",
                  "cn=foo4,cn=bar#'0100'B", "cn=foo5,cn=bar#'0101'B",
                  "cn=foo6,cn=bar#'0110'B"],
     'negative': ["cn=foo1,cn=bar#'0001'B", "cn=foo1,cn=bar#'0001'B",
                  "cn=foo2,cn=bar#'0010'B", "cn=foo2,cn=bar#'0010'B",
                  "cn=foo3,cn=bar#'0011'B", "cn=foo3,cn=bar#'0011'B",
                  "cn=foo4,cn=bar#'0100'B", "cn=foo4,cn=bar#'0100'B",
                  "cn=foo5,cn=bar#'0101'B", "cn=foo5,cn=bar#'0101'B",
                  "cn=foo6,cn=bar#'0110'B", "cn=foo6,cn=bar#'0110'B"]},
    {'attr': 'attrnumericStringMatch',
     'positive': ['00002', '00003', '00004', '00005', '00006'],
     'negative': ['00001', '00001', '00002', '00002', '00003', '00003',
                  '00004', '00004', '00005', '00005', '00006', '00006']},
    {'attr': 'attrtelephoneNumberMatch',
     'positive': ['+1 408 555 5625', '+1 408 555 6201', '+1 408 555 8585',
                  '+1 408 555 9187', '+1 408 555 9423'],
     'negative': ['+1 408 555 4798', '+1 408 555 4798', '+1 408 555 5625',
                  '+1 408 555 5625', '+1 408 555 6201', '+1 408 555 6201',
                  '+1 408 555 8585', '+1 408 555 8585', '+1 408 555 9187',
                  '+1 408 555 9187', '+1 408 555 9423', '+1 408 555 9423']},
    {'attr': 'attroctetStringMatch',
     'positive': ['AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=', 'AAAAAAAAAAAAAAQ=',
                  'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY='],
     'negative': ['AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAI=',
                  'AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=', 'AAAAAAAAAAAAAAM=',
                  'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAU=',
                  'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY=', 'AAAAAAAAAAAAAAY=']}]


LIST_MOD_ATTR_ALL = [
    {'attr': 'attrcaseExactMatch',
     'positive': ['ÇélIné Ändrè'],
     'negative': ['ÇélIné Ändrè', 'ÇéliNé Ändrè', 'Çéliné ÄndrÈ', 'Çéliné Ändrè',
                  'çÉliné Ändrè']},
    {'attr': 'attrgeneralizedTimeMatch',
     'positive': ['20100218171300Z'],
     'negative': ['20100218171300Z', '20100218171301Z', '20100218171302Z',
                  '20100218171303Z', '20100218171304Z', '20100218171305Z']},
    {'attr': 'attrbooleanMatch',
     'positive': ['TRUE'],
     'negative': ['TRUE', 'FALSE']},
    {'attr': 'attrcaseIgnoreIA5Match',
     'positive': ['sprain1'],
     'negative': ['sprain1', 'sprain2', 'sprain3', 'sprain4', 'sprain5', 'sprain6']},
    {'attr': 'attrcaseIgnoreMatch',
     'positive': ['ÇélIné Ändrè1'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6']},
    {'attr': 'attrcaseIgnoreListMatch',
     'positive': ['foo1$bar'],
     'negative': ['foo1$bar', 'foo2$bar', 'foo3$bar', 'foo4$bar', 'foo5$bar', 'foo6$bar']},
    {'attr': 'attrbitStringMatch',
     'positive': ["'0001'B"],
     'negative': ["'0001'B", "'0010'B", "'0011'B", "'0100'B", "'0101'B", "'0110'B"]},
    {'attr': 'attrcaseExactIA5Match',
     'positive': ['Sprain'],
     'negative': ['Sprain', 'sPrain', 'spRain', 'sprAin', 'spraIn', 'sprain']},
    {'attr': 'attrobjectIdentifierMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.15'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdirectoryStringFirstComponentMatch',
     'positive': ['ÇélIné Ändrè1'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6']},
    {'attr': 'attrobjectIdentifierFirstComponentMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.15'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdistinguishedNameMatch',
     'positive': ['cn=foo1,cn=bar'],
     'negative': ['cn=foo1,cn=bar', 'cn=foo2,cn=bar', 'cn=foo3,cn=bar',
                  'cn=foo4,cn=bar', 'cn=foo5,cn=bar', 'cn=foo6,cn=bar']},
    {'attr': 'attrintegerMatch',
     'positive': ['-2'],
     'negative': ['-2', '-1', '0', '1', '2', '3']},
    {'attr': 'attrintegerFirstComponentMatch',
     'positive': ['-2'],
     'negative': ['-2', '-1', '0', '1', '2', '3']},
    {'attr': 'attruniqueMemberMatch',
     'positive': ["cn=foo1,cn=bar#'0001'B"],
     'negative': ["cn=foo1,cn=bar#'0001'B", "cn=foo2,cn=bar#'0010'B",
                  "cn=foo3,cn=bar#'0011'B", "cn=foo4,cn=bar#'0100'B",
                  "cn=foo5,cn=bar#'0101'B", "cn=foo6,cn=bar#'0110'B"]},
    {'attr': 'attrnumericStringMatch',
     'positive': ['00001'],
     'negative': ['00001', '00002', '00003', '00004', '00005', '00006']},
    {'attr': 'attrgeneralizedTimeMatch',
     'positive': ['+1 408 555 4798'],
     'negative': ['+1 408 555 4798', '+1 408 555 5625', '+1 408 555 6201',
                  '+1 408 555 8585', '+1 408 555 9187', '+1 408 555 9423']},
    {'attr': 'attroctetStringMatch',
     'positive': ['AAAAAAAAAAAAAAE='],
     'negative': ['AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=',
                  'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY=']}]


@pytest.fixture(scope="module")
def _create_index_entry(topology_st):
    """Create index entries.
        :id: 9c93aec8-b87d-11e9-93b0-8c16451d917b
        :setup: Standalone
        :steps:
            1. Test index entries can be created.
        :expected results:
            1. Pass
    """
    indexes = Indexes(topology_st.standalone)
    for cn_cn, index_type in LIST_CN_INDEX:
        indexes.create(properties={
            'cn': cn_cn,
            'nsSystemIndex': 'true',
            'nsIndexType': index_type
        })


@pytest.mark.parametrize("index", LIST_ATTR_INDEX)
def test_valid_invalid_attributes(topology_st, _create_index_entry, index):
    """Test valid and invalid values of attributes
        :id: 93dc9e02-b87d-11e9-b39b-8c16451d917b
        :setup: Standalone
        :steps:
            1. Create entry with an attribute that uses that matching rule
            2. Delete existing entry
            3. Create entry with an attribute that uses that matching rule providing duplicate
            values that are duplicates according to the equality matching rule.
        :expected results:
            1. Pass
            2. Pass
            3. Fail(ldap.TYPE_OR_VALUE_EXISTS)
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    # Entry with extensibleObject
    entry = cos.create(properties={'cn': 'addentry' + index['attr'].split('attr')[1],
                                   index['attr']: index['positive']})
    entry.delete()
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        cos.create(properties={'cn': 'addentry' + index['attr'].split('attr')[1],
                               index['attr']: index['negative']})


@pytest.mark.parametrize("mod", LIST_MOD_ATTR_ALL)
def test_mods(topology_st, _create_index_entry, mod):
    """Test valid and invalid values of attributes mods
        :id: 8c15874c-b87d-11e9-9c5d-8c16451d917b
        :setup: Standalone
        :steps:
            1. Create entry with an attribute that uses matching mod
            2. Add an attribute that uses that matching mod providing duplicate
            values that are duplicates according to the equality matching.
            3. Delete existing entry
        :expected results:
            1. Pass
            2. Fail(ldap.TYPE_OR_VALUE_EXISTS)
            3. Pass
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    # Entry with extensibleObject
    cos.create(properties={'cn': 'addentry'+mod['attr'].split('attr')[1],
                           mod['attr']: mod['positive']})
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        cos.list()[0].add(mod['attr'], mod['negative'])
    for entry in cos.list():
        entry.delete()


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

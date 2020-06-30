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
from lib389.schema import Schema

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


LIST_MOD_REPLACE_ALL = [
    {'attr': 'attrcaseExactIA5Match',
     'positive': ['Sprain', 'sPrain', 'spRain', 'sprAin', 'spraIn', 'sprain'],
     'negative': ['Sprain', 'Sprain', 'sPrain', 'sPrain', 'spRain', 'spRain',
                  'sprAin', 'sprAin', 'spraIn', 'spraIn', 'sprain', 'sprain']},
    {'attr': 'attrcaseExactMatch',
     'positive': ['ÇélIné Ändrè', 'ÇéliNé Ändrè', 'Çéliné ÄndrÈ',
                  'Çéliné Ändrè', 'çÉliné Ändrè'],
     'negative': ['ÇélIné Ändrè', 'ÇélIné Ändrè', 'ÇéliNé Ändrè', 'ÇéliNé Ändrè',
                  'Çéliné ÄndrÈ', 'Çéliné ÄndrÈ', 'Çéliné Ändrè', 'Çéliné Ändrè',
                  'çÉliné Ändrè', 'çÉliné Ändrè']},
    {'attr': 'attrbitStringMatch',
     'positive': ["'0001'B", "'0010'B", "'0011'B", "'0100'B", "'0101'B", "'0110'B"],
     'negative': ["'0001'B", "'0001'B", "'0010'B", "'0010'B", "'0011'B", "'0011'B",
                  "'0100'B", "'0100'B", "'0101'B", "'0101'B", "'0110'B", "'0110'B"]},
    {'attr': 'attrgeneralizedTimeMatch',
     'positive': ['20100218171300Z', '20100218171301Z', '20100218171302Z',
                  '20100218171303Z', '20100218171304Z', '20100218171305Z'],
     'negative': ['20100218171300Z', '20100218171300Z', '20100218171301Z',
                  '20100218171301Z', '20100218171302Z', '20100218171302Z',
                  '20100218171303Z', '20100218171303Z', '20100218171304Z',
                  '20100218171304Z', '20100218171305Z', '20100218171305Z']},
    {'attr': 'attrbooleanMatch',
     'positive': ['TRUE', 'FALSE'],
     'negative': ['TRUE', 'TRUE', 'FALSE', 'FALSE']},
    {'attr': 'attrcaseIgnoreIA5Match',
     'positive': ['sprain1', 'sprain2', 'sprain3', 'sprain4', 'sprain5', 'sprain6'],
     'negative': ['sprain1', 'sprain1', 'sprain2', 'sprain2', 'sprain3', 'sprain3',
                  'sprain4', 'sprain4', 'sprain5', 'sprain5', 'sprain6', 'sprain6']},
    {'attr': 'attrcaseIgnoreMatch',
     'positive': ['ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè2',
                  'ÇélIné Ändrè3', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']},
    {'attr': 'attrcaseIgnoreListMatch',
     'positive': ['foo1$bar', 'foo2$bar', 'foo3$bar', 'foo4$bar', 'foo5$bar', 'foo6$bar'],
     'negative': ['foo1$bar', 'foo1$bar', 'foo2$bar', 'foo2$bar', 'foo3$bar', 'foo3$bar',
                  'foo4$bar', 'foo4$bar', 'foo5$bar', 'foo5$bar', 'foo6$bar', 'foo6$bar']},
    {'attr': 'attrobjectIdentifierMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.6'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                  '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdirectoryStringFirstComponentMatch',
     'positive': ['ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè6'],
     'negative': ['ÇélIné Ändrè1', 'ÇélIné Ändrè1', 'ÇélIné Ändrè2', 'ÇélIné Ändrè2',
                  'ÇélIné Ändrè3', 'ÇélIné Ändrè3', 'ÇélIné Ändrè4', 'ÇélIné Ändrè4',
                  'ÇélIné Ändrè5', 'ÇélIné Ändrè5', 'ÇélIné Ändrè6', 'ÇélIné Ändrè6']},
    {'attr': 'attrobjectIdentifierFirstComponentMatch',
     'positive': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.6'],
     'negative': ['1.3.6.1.4.1.1466.115.121.1.15', '1.3.6.1.4.1.1466.115.121.1.15',
                  '1.3.6.1.4.1.1466.115.121.1.24', '1.3.6.1.4.1.1466.115.121.1.24',
                  '1.3.6.1.4.1.1466.115.121.1.26', '1.3.6.1.4.1.1466.115.121.1.26',
                  '1.3.6.1.4.1.1466.115.121.1.40', '1.3.6.1.4.1.1466.115.121.1.40',
                  '1.3.6.1.4.1.1466.115.121.1.41', '1.3.6.1.4.1.1466.115.121.1.41',
                  '1.3.6.1.4.1.1466.115.121.1.6', '1.3.6.1.4.1.1466.115.121.1.6']},
    {'attr': 'attrdistinguishedNameMatch',
     'positive': ['cn=foo1,cn=bar', 'cn=foo2,cn=bar', 'cn=foo3,cn=bar', 'cn=foo4,cn=bar',
                  'cn=foo5,cn=bar', 'cn=foo6,cn=bar'],
     'negative': ['cn=foo1,cn=bar', 'cn=foo1,cn=bar', 'cn=foo2,cn=bar', 'cn=foo2,cn=bar',
                  'cn=foo3,cn=bar', 'cn=foo3,cn=bar', 'cn=foo4,cn=bar', 'cn=foo4,cn=bar',
                  'cn=foo5,cn=bar', 'cn=foo5,cn=bar', 'cn=foo6,cn=bar', 'cn=foo6,cn=bar']},
    {'attr': 'attrintegerMatch',
     'positive': ['-2', '-1', '0', '1', '2', '3'],
     'negative': ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']},
    {'attr': 'attrintegerFirstComponentMatch',
     'positive': ['-2', '-1', '0', '1', '2', '3'],
     'negative': ['-2', '-2', '-1', '-1', '0', '0', '1', '1', '2', '2', '3', '3']},
    {'attr': 'attruniqueMemberMatch',
     'positive': ["cn=foo1,cn=bar#'0001'B", "cn=foo2,cn=bar#'0010'B",
                  "cn=foo3,cn=bar#'0011'B", "cn=foo4,cn=bar#'0100'B",
                  "cn=foo5,cn=bar#'0101'B", "cn=foo6,cn=bar#'0110'B"],
     'negative': ["cn=foo1,cn=bar#'0001'B", "cn=foo1,cn=bar#'0001'B",
                  "cn=foo2,cn=bar#'0010'B", "cn=foo2,cn=bar#'0010'B",
                  "cn=foo3,cn=bar#'0011'B", "cn=foo3,cn=bar#'0011'B",
                  "cn=foo4,cn=bar#'0100'B", "cn=foo4,cn=bar#'0100'B",
                  "cn=foo5,cn=bar#'0101'B", "cn=foo5,cn=bar#'0101'B",
                  "cn=foo6,cn=bar#'0110'B", "cn=foo6,cn=bar#'0110'B"]},
    {'attr': 'attrnumericStringMatch',
     'positive': ['00001', '00002', '00003', '00004', '00005', '00006'],
     'negative': ['00001', '00001', '00002', '00002', '00003', '00003',
                  '00004', '00004', '00005', '00005', '00006', '00006']},
    {'attr': 'attrtelephoneNumberMatch',
     'positive': ['+1 408 555 4798', '+1 408 555 5625', '+1 408 555 6201',
                  '+1 408 555 8585', '+1 408 555 9187', '+1 408 555 9423'],
     'negative': ['+1 408 555 4798', '+1 408 555 4798', '+1 408 555 5625',
                  '+1 408 555 5625', '+1 408 555 6201', '+1 408 555 6201',
                  '+1 408 555 8585', '+1 408 555 8585', '+1 408 555 9187',
                  '+1 408 555 9187', '+1 408 555 9423', '+1 408 555 9423']},
    {'attr': 'attroctetStringMatch',
     'positive': ['AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=',
                  'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY='],
     'negative': ['AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAE=', 'AAAAAAAAAAAAAAI=',
                  'AAAAAAAAAAAAAAI=', 'AAAAAAAAAAAAAAM=', 'AAAAAAAAAAAAAAM=',
                  'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAQ=', 'AAAAAAAAAAAAAAU=',
                  'AAAAAAAAAAAAAAU=', 'AAAAAAAAAAAAAAY=', 'AAAAAAAAAAAAAAY=']}]


LIST_MOD_DEL_ALL = [
    {'attr': 'attrbitStringMatch',
     'positive_negative': ["'0001'B"]},
    {'attr': 'attrcaseExactIA5Match',
     'positive_negative': ['Sprain']},
    {'attr': 'attrbitStringMatch',
     'positive_negative': ["'0001'B"]},
    {'attr': 'attrcaseExactMatch',
     'positive_negative': ['ÇélIné Ändrè']},
    {'attr': 'attrgeneralizedTimeMatch',
     'positive_negative': ['20100218171300Z']},
    {'attr': 'attrbooleanMatch',
     'positive_negative': ['TRUE']},
    {'attr': 'attrcaseIgnoreIA5Match',
     'positive_negative': ['sprain1']},
    {'attr': 'attrcaseIgnoreMatch',
     'positive_negative': ['ÇélIné Ändrè1']},
    {'attr': 'attrcaseIgnoreListMatch',
     'positive_negative': ['foo1$bar']},
    {'attr': 'attrobjectIdentifierMatch',
     'positive_negative': ['1.3.6.1.4.1.1466.115.121.1.15']},
    {'attr': 'attrdirectoryStringFirstComponentMatch',
     'positive_negative': ['ÇélIné Ändrè1']},
    {'attr': 'attrintegerMatch',
     'positive_negative': ['-2']},
    {'attr': 'attrintegerFirstComponentMatch',
     'positive_negative': ['cn=foo1,cn=bar']},
    {'attr': 'attrintegerFirstComponentMatch',
     'positive_negative': ['-2']},
    {'attr': 'attruniqueMemberMatch',
     'positive_negative': ["cn=foo1,cn=bar#'0001'B"]},
    {'attr': 'attrnumericStringMatch',
     'positive_negative': ['00001']},
    {'attr': 'attrtelephoneNumberMatch',
     'positive_negative': ['+1 408 555 4798']},
    {'attr': 'attroctetStringMatch',
     'positive_negative': ['AAAAAAAAAAAAAAE=']}]


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
    :parametrized: yes
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
    entry = cos.create(properties={'cn': 'addentry' + index['attr'],
                                   index['attr']: index['positive']})
    entry.delete()
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        cos.create(properties={'cn': 'addentry' + index['attr'].split('attr')[1],
                               index['attr']: index['negative']})


@pytest.mark.parametrize("mod", LIST_MOD_ATTR_ALL)
def test_mods(topology_st, _create_index_entry, mod):
    """Test valid and invalid values of attributes mods

    :id: 8c15874c-b87d-11e9-9c5d-8c16451d917b
    :parametrized: yes
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
    entry = cos.create(properties={'cn': 'addentry'+mod['attr'],
                                   mod['attr']: mod['positive']})
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        entry.add(mod['attr'], mod['negative'])
    entry.delete()


@pytest.mark.parametrize("mode", LIST_MOD_REPLACE_ALL)
def test_mods_replace(topology_st, _create_index_entry, mode):
    """Test mods replace

    :id: 2dd46b7a-b928-11e9-91dd-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Create entry with an attribute that uses matching mode
        2. Add an attribute that uses that matching mode providing duplicate
           values that are duplicates according to the equality matching.
        3. Delete existing entry
    :expected results:
        1. Pass
        2. Fail(ldap.TYPE_OR_VALUE_EXISTS)
        3. Pass
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    # Entry with extensibleObject
    entry = cos.create(properties={'cn': 'addentry'+mode['attr'],
                                   mode['attr']: mode['positive']})
    with pytest.raises(ldap.TYPE_OR_VALUE_EXISTS):
        entry.replace(mode['attr'], mode['negative'])
    entry.delete()


@pytest.mark.parametrize("mode", LIST_MOD_DEL_ALL)
def test_mods_delete(topology_st, _create_index_entry, mode):
    """Test mods delete

    :id: 1dda055e-b928-11e9-b5c1-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Create entry with an attribute that uses matching mode
        2. Add an attribute that uses that matching mode providing duplicate
           values that are duplicates according to the equality matching.
        3. Delete existing entry
    :expected results:
        1. Pass
        2. Fail(ldap.NO_SUCH_ATTRIBUTE)
        3. Pass
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    # Entry with extensibleObject
    entry = cos.create(properties={'cn': 'addentry'+mode['attr'],
                                   mode['attr']: mode['positive_negative']})
    entry.remove(mode['attr'], mode['positive_negative'][0])
    with pytest.raises(ldap.NO_SUCH_ATTRIBUTE):
        entry.remove(mode['attr'], mode['positive_negative'][0])
    entry.delete()


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


LIST_ATTR_TO_CREATE = [
    ('entryoctetStringMatch0', 'AAAAAAAAAAAAAAE='),
    ('entryoctetStringMatch1', 'AAAAAAAAAAAAAAI='),
    ('entryoctetStringMatch2', 'AAAAAAAAAAAAAAM='),
    ('entryoctetStringMatch3', 'AAAAAAAAAAAAAAQ='),
    ('entryoctetStringMatch4', 'AAAAAAAAAAAAAAU='),
    ('entryoctetStringMatch5', 'AAAAAAAAAAAAAAY='),
    ('entrybitStringMatch0', "'0001'B"),
    ('entrybitStringMatch1', "'0010'B"),
    ('entrybitStringMatch2', "'0011'B"),
    ('entrybitStringMatch3', "'0100'B"),
    ('entrybitStringMatch4', "'0101'B"),
    ('entrybitStringMatch5', "'0110'B"),
    ('entrycaseExactIA5Match0', "Sprain"),
    ('entrycaseExactIA5Match1', "sPrain"),
    ('entrycaseExactIA5Match2', "spRain"),
    ('entrycaseExactIA5Match3', "sprAin"),
    ('entrycaseExactIA5Match4', "spraIn"),
    ('entrycaseExactIA5Match5', "sprain"),
    ('entrycaseExactMatch0', "ÇélIné Ändrè"),
    ('entrycaseExactMatch1', "ÇéliNé Ändrè"),
    ('entrycaseExactMatch2', "Çéliné ÄndrÈ"),
    ('entrycaseExactMatch3', "Çéliné Ändrè"),
    ('entrycaseExactMatch4', "çÉliné Ändrè"),
    ('entrygeneralizedTimeMatch0', "20100218171300Z"),
    ('entrygeneralizedTimeMatch1', "20100218171301Z"),
    ('entrygeneralizedTimeMatch2', "20100218171302Z"),
    ('entrygeneralizedTimeMatch3', "20100218171303Z"),
    ('entrygeneralizedTimeMatch4', "20100218171304Z"),
    ('entrygeneralizedTimeMatch5', "20100218171305Z"),
    ('entrybooleanMatch0', "TRUE"),
    ('entrybooleanMatch1', "FALSE"),
    ('entrycaseIgnoreIA5Match0', "sprain1"),
    ('entrycaseIgnoreIA5Match1', "sprain2"),
    ('entrycaseIgnoreIA5Match2', "sprain3"),
    ('entrycaseIgnoreIA5Match3', "sprain4"),
    ('entrycaseIgnoreIA5Match4', "sprain5"),
    ('entrycaseIgnoreIA5Match5', "sprain6"),
    ('entrycaseIgnoreMatch0', "ÇélIné Ändrè1"),
    ('entrycaseIgnoreMatch1', "ÇélIné Ändrè2"),
    ('entrycaseIgnoreMatch2', "ÇélIné Ändrè3"),
    ('entrycaseIgnoreMatch3', "ÇélIné Ändrè4"),
    ('entrycaseIgnoreMatch4', "ÇélIné Ändrè5"),
    ('entrycaseIgnoreMatch5', "ÇélIné Ändrè6"),
    ('entrycaseIgnoreListMatch0', "foo1$bar"),
    ('entrycaseIgnoreListMatch1', "foo2$bar"),
    ('entrycaseIgnoreListMatch2', "foo3$bar"),
    ('entrycaseIgnoreListMatch3', "foo4$bar"),
    ('entrycaseIgnoreListMatch4', "foo5$bar"),
    ('entrycaseIgnoreListMatch5', "foo6$bar"),
    ('entryobjectIdentifierMatch0', "1.3.6.1.4.1.1466.115.121.1.15"),
    ('entryobjectIdentifierMatch1', "1.3.6.1.4.1.1466.115.121.1.24"),
    ('entryobjectIdentifierMatch2', "1.3.6.1.4.1.1466.115.121.1.26"),
    ('entryobjectIdentifierMatch3', "1.3.6.1.4.1.1466.115.121.1.40"),
    ('entryobjectIdentifierMatch4', "1.3.6.1.4.1.1466.115.121.1.41"),
    ('entryobjectIdentifierMatch5', "1.3.6.1.4.1.1466.115.121.1.6"),
    ('entrydistinguishedNameMatch0', "cn=foo1,cn=bar"),
    ('entrydistinguishedNameMatch1', "cn=foo2,cn=bar"),
    ('entrydistinguishedNameMatch2', "cn=foo3,cn=bar"),
    ('entrydistinguishedNameMatch3', "cn=foo4,cn=bar"),
    ('entrydistinguishedNameMatch4', "cn=foo5,cn=bar"),
    ('entrydistinguishedNameMatch5', "cn=foo6,cn=bar"),
    ('entryintegerMatch0', "-2"),
    ('entryintegerMatch1', "-1"),
    ('entryintegerMatch2', "0"),
    ('entryintegerMatch3', "1"),
    ('entryintegerMatch4', "2"),
    ('entryintegerMatch5', "3"),
    ('entryuniqueMemberMatch0', "cn=foo1,cn=bar#'0001'B"),
    ('entryuniqueMemberMatch1', "cn=foo2,cn=bar#'0010'B"),
    ('entryuniqueMemberMatch2', "cn=foo3,cn=bar#'0011'B"),
    ('entryuniqueMemberMatch3', "cn=foo4,cn=bar#'0100'B"),
    ('entryuniqueMemberMatch4', "cn=foo5,cn=bar#'0101'B"),
    ('entryuniqueMemberMatch5', "cn=foo6,cn=bar#'0110'B"),
    ('entrynumericStringMatch0', "00001"),
    ('entrynumericStringMatch1', "00002"),
    ('entrynumericStringMatch2', "00003"),
    ('entrynumericStringMatch3', "00004"),
    ('entrynumericStringMatch4', "00005"),
    ('entrynumericStringMatch5', "00006"),
    ('entrytelephoneNumberMatch0', "+1 408 555 4798"),
    ('entrytelephoneNumberMatch1', "+1 408 555 5625"),
    ('entrytelephoneNumberMatch2', "+1 408 555 6201"),
    ('entrytelephoneNumberMatch3', "+1 408 555 8585"),
    ('entrytelephoneNumberMatch4', "+1 408 555 9187"),
    ('entrytelephoneNumberMatch5', "+1 408 555 9423"),
    ('entrydirectoryStringFirstComponentMatch0', "ÇélIné Ändrè1"),
    ('entrydirectoryStringFirstComponentMatch1', "ÇélIné Ändrè2"),
    ('entrydirectoryStringFirstComponentMatch2', "ÇélIné Ändrè3"),
    ('entrydirectoryStringFirstComponentMatch3', "ÇélIné Ändrè4"),
    ('entrydirectoryStringFirstComponentMatch4', "ÇélIné Ändrè5"),
    ('entrydirectoryStringFirstComponentMatch5', "ÇélIné Ändrè6"),
    ('entryobjectIdentifierFirstComponentMatch0', "1.3.6.1.4.1.1466.115.121.1.15"),
    ('entryobjectIdentifierFirstComponentMatch1', "1.3.6.1.4.1.1466.115.121.1.24"),
    ('entryobjectIdentifierFirstComponentMatch2', "1.3.6.1.4.1.1466.115.121.1.26"),
    ('entryobjectIdentifierFirstComponentMatch3', "1.3.6.1.4.1.1466.115.121.1.40"),
    ('entryobjectIdentifierFirstComponentMatch4', "1.3.6.1.4.1.1466.115.121.1.41"),
    ('entryobjectIdentifierFirstComponentMatch5', "1.3.6.1.4.1.1466.115.121.1.6"),
    ('entryintegerFirstComponentMatch0', "-2"),
    ('entryintegerFirstComponentMatch1', "-1"),
    ('entryintegerFirstComponentMatch2', "0"),
    ('entryintegerFirstComponentMatch3', "1"),
    ('entryintegerFirstComponentMatch4', "2"),
    ('entryintegerFirstComponentMatch5', "3")]


@pytest.fixture(scope="module")
def _create_entries(topology_st):
    """
    Add attribute types to schema and Create filter
    entries(Entry with extensibleObject)
    """
    for attribute in ATTR:
        Schema(topology_st.standalone).add('attributetypes', attribute)
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    # Entry with extensibleObject
    for attr, value in LIST_ATTR_TO_CREATE:
        cos.create(properties={
            'cn': attr,
            'attr' + attr.split('entry')[1][:-1]: value
        })


FILTER_VALUES = [
    ["(attrbitStringMatch='0001'B)", 1,
     "(attrbitStringMatch:bitStringMatch:='000100000'B)"],
    ["(attrgeneralizedTimeMatch=20100218171300Z)", 1,
     "(attrcaseExactIA5Match=SPRAIN)"],
    ["(attrcaseExactMatch>=ÇélIné Ändrè)", 5,
     "(attrcaseExactMatch=ÇéLINé ÄNDRè)"],
    ["(attrcaseExactMatch:caseExactMatch:=ÇélIné Ändrè)", 1,
     "(attrcaseExactMatch>=çéliné ändrè)"],
    ["(attrcaseExactIA5Match=Sprain)", 1,
     "(attrgeneralizedTimeMatch=20300218171300Z)"],
    ["(attrbooleanMatch=TRUE)", 1,
     "(attrgeneralizedTimeMatch>=20300218171300Z)"],
    ["(attrcaseIgnoreIA5Match=sprain1)", 1,
     "(attrcaseIgnoreIA5Match=sprain9999)"],
    ["(attrcaseIgnoreMatch=ÇélIné Ändrè1)", 1,
     "(attrcaseIgnoreMatch=ÇélIné Ändrè9999)"],
    ["(attrcaseIgnoreMatch>=ÇélIné Ändrè1)", 6,
     "(attrcaseIgnoreMatch>=ÇélIné Ändrè9999)"],
    ["(attrcaseIgnoreListMatch=foo1$bar)", 1,
     "(attrcaseIgnoreListMatch=foo1$bar$baz$biff)"],
    ["(attrobjectIdentifierMatch=1.3.6.1.4.1.1466.115.121.1.15)", 1,
     "(attrobjectIdentifierMatch=1.3.6.1.4.1.1466.115.121.1.15.99999)"],
    ["(attrgeneralizedTimeMatch>=20100218171300Z)", 6,
     "(attrdirectoryStringFirstComponentMatch=ÇélIné Ändrè9999)"],
    ["(attrdirectoryStringFirstComponentMatch=ÇélIné Ändrè1)", 1,
     "(attrobjectIdentifierFirstComponentMatch=1.3.6.1.4.1.1466.115.121.1.15.99999)"],
    ["(attrobjectIdentifierFirstComponentMatch=1.3.6.1.4.1.1466.115.121.1.15)", 1,
     "(attrdistinguishedNameMatch=cn=foo1,cn=bar,cn=baz)"],
    ["(attrdistinguishedNameMatch=cn=foo1,cn=bar)", 1,
     "(attrintegerMatch=-20)"],
    ["(attrintegerMatch=-2)", 1,
     "(attrintegerMatch>=20)"],
    ["(attrintegerMatch>=-2)", 6,
     "(attrintegerFirstComponentMatch=-20)"],
    ["(attrintegerFirstComponentMatch=-2)", 1,
     "(attruniqueMemberMatch=cn=foo1,cn=bar#'00010000'B)"],
    ["(attruniqueMemberMatch=cn=foo1,cn=bar#'0001'B)", 1,
     "(attrnumericStringMatch=000000001)"],
    ["(attrnumericStringMatch=00001)", 1,
     "(attrnumericStringMatch>=01)"],
    ["(attrnumericStringMatch>=00001)", 6,
     "(attrtelephoneNumberMatch=+2 408 555 4798)"],
    ["(attrtelephoneNumberMatch=+1 408 555 4798)", 1,
     "(attroctetStringMatch=AAAAAAAAAAAAAAEB)"],
    ["(attroctetStringMatch=AAAAAAAAAAAAAAE=)", 1,
     "(attroctetStringMatch>=AAAAAAAAAAABAQE=)"],
    ["(attroctetStringMatch>=AAAAAAAAAAAAAAE=)", 6,
     "(attrdirectoryStringFirstComponentMatch=ÇélIné Ändrè9998)"]]


def test_search_positive_negative(topology_st, _create_entries):
    """Filters with positive and with no output.

    :id: abe3e6dd-9ecc-12e8-adf0-8c16451d917b
    :parametrized: yes
    :setup: Standalone
    :steps:
        1.For valid filer output should match the exact value given.
        2. For invalid filter there should not be any output.
    :expected results:
        1. Pass
        2. Pass
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    for attr, value, negative_filter in FILTER_VALUES:
        assert len(cos.filter(attr)) == value
        assert not cos.filter(negative_filter)


LIST_EXT_ATTR_COUNT = [
    ("(attrbitStringMatch:bitStringMatch:='0001'B)", 1),
    ("(attrcaseExactIA5Match:caseExactIA5Match:=Sprain)", 1),
    ("(attrcaseExactMatch:caseExactMatch:=ÇélIné Ändrè)", 1),
    ("(attrcaseExactMatch:caseExactOrderingMatch:=ÇélIné Ändrè)", 5),
    ("(attrgeneralizedTimeMatch:generalizedTimeMatch:=20100218171300Z)", 1),
    ("(attrgeneralizedTimeMatch:generalizedTimeOrderingMatch:=20100218171300Z)", 6),
    ("(attrbooleanMatch:booleanMatch:=TRUE)", 1),
    ("(attrcaseIgnoreIA5Match:caseIgnoreIA5Match:=sprain1)", 1),
    ("(attrcaseIgnoreMatch:caseIgnoreMatch:=ÇélIné Ändrè1)", 1),
    ("(attrcaseIgnoreMatch:caseIgnoreOrderingMatch:=ÇélIné Ändrè1)", 6),
    ("(attrcaseIgnoreListMatch:caseIgnoreListMatch:=foo1$bar)", 1),
    ("(attrobjectIdentifierMatch:objectIdentifierMatch:=1.3.6.1.4.1.1466.115.121.1.15)", 1),
    ("(attrdirectoryStringFirstComponentMatch:directoryString"
     "FirstComponentMatch:=ÇélIné Ändrè1)", 1),
    ("(attrobjectIdentifierFirstComponentMatch:objectIdentifier"
     "FirstComponentMatch:=1.3.6.1.4.1.1466.115.121.1.15)", 1),
    ("(attrdistinguishedNameMatch:distinguishedNameMatch:=cn=foo1,cn=bar)", 1),
    ("(attrintegerMatch:integerMatch:=-2)", 1),
    ("(attrintegerMatch:integerOrderingMatch:=-2)", 6),
    ("(attrintegerFirstComponentMatch:integerFirstComponentMatch:=-2)", 1),
    ("(attruniqueMemberMatch:uniqueMemberMatch:=cn=foo1,cn=bar#'0001'B)", 1),
    ("(attrnumericStringMatch:numericStringMatch:=00001)", 1),
    ("(attrnumericStringMatch:numericStringMatch:=00001)", 1),
    ("(attrtelephoneNumberMatch:telephoneNumberMatch:=+1 408 555 4798)", 1),
    ("(attroctetStringMatch:octetStringMatch:=AAAAAAAAAAAAAAE=)", 1),
    ("(attroctetStringMatch:octetStringOrderingMatch:=AAAAAAAAAAAAAAE=)", 6),
    ("(attrcaseExactMatch=*ÇélIné Ändrè*)", 1),
    ("(attrcaseExactMatch=ÇélIné Ändrè*)", 1),
    ("(attrcaseExactMatch=*ÇélIné Ändrè)", 1),
    ("(attrcaseExactMatch=*é Ä*)", 5),
    ("(attrcaseExactIA5Match=*Sprain*)", 1),
    ("(attrcaseExactIA5Match=Sprain*)", 1),
    ("(attrcaseExactIA5Match=*Sprain)", 1),
    ("(attrcaseExactIA5Match=*rai*)", 3),
    ("(attrcaseIgnoreIA5Match=*sprain1*)", 1),
    ("(attrcaseIgnoreIA5Match=sprain1*)", 1),
    ("(attrcaseIgnoreIA5Match=*sprain1)", 1),
    ("(attrcaseIgnoreIA5Match=*rai*)", 6),
    ("(attrcaseIgnoreMatch=*ÇélIné Ändrè1*)", 1),
    ("(attrcaseIgnoreMatch=ÇélIné Ändrè1*)", 1),
    ("(attrcaseIgnoreMatch=*ÇélIné Ändrè1)", 1),
    ("(attrcaseIgnoreMatch=*é Ä*)", 6),
    ("(attrcaseIgnoreListMatch=*foo1$bar*)", 1),
    ("(attrcaseIgnoreListMatch=foo1$bar*)", 1),
    ("(attrcaseIgnoreListMatch=*foo1$bar)", 1),
    ("(attrcaseIgnoreListMatch=*1$b*)", 1),
    ("(attrnumericStringMatch=*00001*)", 1),
    ("(attrnumericStringMatch=00001*)", 1),
    ("(attrnumericStringMatch=*00001)", 1),
    ("(attrnumericStringMatch=*000*)", 6),
    ("(attrtelephoneNumberMatch=*+1 408 555 4798*)", 1),
    ("(attrtelephoneNumberMatch=+1 408 555 4798*)", 1),
    ("(attrtelephoneNumberMatch=*+1 408 555 4798)", 1),
    ("(attrtelephoneNumberMatch=* 55*)", 6)]


@pytest.mark.parametrize("attr, value", LIST_EXT_ATTR_COUNT)
def test_do_extensible_search(topology_st, _create_entries, attr, value):
    """Match filter and output.

    :id: abe3e6dd-9ecc-11e8-adf0-8c16451d917c
    :parametrized: yes
    :setup: Standalone
    :steps:
        1. Filer output should match the exact value given.
    :expected results:
        1. Pass
    """
    cos = CosTemplates(topology_st.standalone, DEFAULT_SUFFIX)
    assert len(cos.filter(attr)) == value


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

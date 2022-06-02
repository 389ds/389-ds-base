# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

"""
verify and testing  Filter from a search
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.account import Accounts
from lib389.idm.user import UserAccount, UserAccounts
from lib389.schema import Schema
from lib389.idm.role import ManagedRoles, FilteredRoles

pytestmark = pytest.mark.tier1

FILTER_POSTAL = "(postalCode=99999)"
FILTER_ADDRESS = "(postalAddress=345 California Av., Mountain View, CA)"
FILTER_8888 = "(postalCode:2.16.840.1.113730.3.3.2.7.1:=88888)"
FILTER_6666 = "(postalCode:2.16.840.1.113730.3.3.2.7.1.3:=66666)"
FILTER_VPE = "(emailclass=vpe*)"
FILTER_EMAIL = "(emailclass=*emai*)"
FILTER_EMAILQUATA = "(mailquota=*00)"
FILTER_QUATA = '(mailquota=*6*0)'
FILTER_ROLE = '(nsRole=*)'
FILTER_POST = '(postalAddress=*)'
FILTER_CLASS = "(emailclass:2.16.840.1.113730.3.3.2.15.1:=>AAA)"
FILTER_CLASSES = "(emailclass:es:=>AAA)"
FILTER_AAA = "(emailclass:2.16.840.1.113730.3.3.2.15.1.5:=AAA)"
FILTER_VE = "(emailclass:2.16.840.1.113730.3.3.2.15.1:=>vpemail)"
FILTER_VPEM = "(emailclass:es:=>vpemail)"
FILTER_900 = "(mailquota:2.16.840.1.113730.3.3.2.15.1.1:=900)"
FILTER_7777 = "(postalCode:de:==77777)"
FILTER_FRED = '(fred=*)'
FILTER_ECLASS = "(emailclass:2.16.840.1.113730.3.3.2.15.1.5:=vpemail)"
FILTER_ECLASS_1 = "(emailclass:2.16.840.1.113730.3.3.2.15.1:=<1)"
FILTER_ECLASS_2 = "(emailclass:es:=<1)"
FILTER_ECLASS_3 = "(emailclass:2.16.840.1.113730.3.3.2.15.1.1:=1)"
FILTER_ECLASS_4 = "(emailclass:2.16.840.1.113730.3.3.2.15.1:=<vpemail)"
FILTER_VPE_1 = "(emailclass:es:=<vpemail)"
FILTER_VPE_2 = "(emailclass:2.16.840.1.113730.3.3.2.15.1.1:=vpemail)"
FILTER_MAILQUATA_1 = "(mailquota:2.16.840.1.113730.3.3.2.15.1:=<900)"
FILTER_MAILQUATA_2 = "(mailquota:es:=<900)"
FILTER_MAIL_900 = "(mailquota<=900)"
FILTER_MAIL_600 = "(mailquota<=600)"
FILTER_MAIL_100 = "(mailquota<=100)"
FILTER_MAIL_100_L = "(mailquota>=100)"
FILTER_MAIL_600_G = "(mailquota>=600)"
FILTER_MAIL_900_G = "(mailquota>=900)"
FILTER_MAIL_100_EL = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=<=100)"
FILTER_MAIL_FR = "(mailquota:fr:=<=100)"
FILTER_MAIL_100_E = "(mailquota:2.16.840.1.113730.3.3.2.18.1.2:=100)"
FILTER_MAIL_600_ELE = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=<=600)"
FILTER_MAIL_FR_600 = "(mailquota:fr:=<=600)"
FILTER_MAIL_E_600 = "(mailquota:2.16.840.1.113730.3.3.2.18.1.2:=600)"
FILTER_MAIL_ELE_900 = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=<=900)"
FILTER_MAIL_FR_ELE_900 = "(mailquota:fr:=<=900)"
FILTER_MAIL_E_900 = "(mailquota:2.16.840.1.113730.3.3.2.18.1.2:=900)"
FILTER_MAIL_EGE_900 = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=>=900)"
FILTER_MAIL_FR_EGE_900 = "(mailquota:fr:=>=900)"
FILTER_MAIL_E_900_2 = "(mailquota:2.16.840.1.113730.3.3.2.18.1.4:=900)"
FILTER_MAIL_EGE_600 = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=>=600)"
FILTER_MAIL_FR_EGE_600 = "(mailquota:fr:=>=600)"
FILTER_MAIL_E_600_2 = "(mailquota:2.16.840.1.113730.3.3.2.18.1.4:=600)"
FILTER_MAIL_EGE_100 = "(mailquota:2.16.840.1.113730.3.3.2.18.1:=>=100)"
FILTER_MAIL_FR_EGE_100 = "(mailquota:fr:=>=100)"
FILTER_MAIL_E_100 = "(mailquota:2.16.840.1.113730.3.3.2.18.1.4:=100)"
FILTER_NS_MANAGE = "(nsRole~=cn=new managed)"
FILTER_NS_NEW = "(nsRole~=cn=new)"
FILTER_ATTRIBUTE = "(Description=This is the special \\2a attribute value)"
FILTER_DES = "(Description=*\\2a*)"
FILTER_VADDER = "(nsRole=cn=new vaddr filtered role,dc=example,dc=com)"
FILTER_WAL = "(uid=*wal*)"
FILTER_ANOTHER = "(nsRole=cn=*another*)"
FILTER_MW = "(uid=mw*)"
FILTER_RN = "(roomNumber=0312)"
FILTER_L = "(l=Cupertino)"
FILTER_USER1 = "(uid=user1)"
FILTER_OBJECT = "(objectclass=inetorgperson)"
FILTER_NS_VADD = "(nsRole=cn=*vaddr*)"
FILTER_L_S = "(l=sunnyvale)"
FILTER_USER20 = "(uid=user20)"
FILTER_USER30 = "(uid=user30)"
FILTER_CN = "(nsRole=cn=another vaddr role,dc=example,dc=com)"
FILTER_RN_3924 = "(roomNumber=3924)"
FILTER_RN_4508 = "(roomNumber=4508)"
FILTER_USER40 = "(uid=user40)"
FILTER_RN_2254 = "(roomNumber=2254)"
FILTER_L_ALL = "(l=*)"
FILTER_OBJ_ALL = "(objectclass=*)"
FILTER_NSROLE = "(nsRole~=cn=new managed)"
FILTER_NSROLE_CN = "(nsRole~=cn=new)"
FILTER_NSROLE_VAD = "(nsRole=cn=new vaddr filtered role,dc=example,dc=com)"
FILTER_UID = "(uid=*wal*)"
FILTER_NSROLE_ANO = "(nsRole=cn=*another*)"
FILTER_UID_USER1 = "(uid=user1)"
FILTER_OBJ = "(objectclass=inetorgperson)"
FILTER_NSROLE_VADR = "(nsRole=cn=*vaddr*)"
FILTER_UID_USER20 = "(uid=user20)"
FILTER_UID_USER30 = "(uid=user30)"
FILTER_NSROLE_ANOV = "(nsRole=cn=another vaddr role,dc=example,dc=com)"
FILTER_UID_USER40 = "(uid=user40)"
FILTER_C_NS_VAD = f"(&{FILTER_NSROLE} {FILTER_NSROLE_VAD})"
FILTER_C_N_NS_VAD = f"(&(!{FILTER_NSROLE})(!{FILTER_NSROLE_VAD}))"
FILTER_C_O_N_NS_VAD = f"(&(!{FILTER_NSROLE}) {FILTER_NSROLE_VAD})"
FILTER_C_N_NS_O_VAD = f"(&{FILTER_NSROLE}(!{FILTER_NSROLE_VAD}))"
FILTER_C_UID_NS = f"(|{FILTER_UID} {FILTER_NSROLE_ANO})"
FILTER_C_N_UID_NS = f"(|(!{FILTER_UID})(!{FILTER_NSROLE_ANO}))"
FILTER_C_N_UID_NSO = f"(|(!{FILTER_UID}){FILTER_NSROLE_ANO})"
FILTER_C_UID_N_NSO = f"(|{FILTER_UID}(!{FILTER_NSROLE_ANO}))"
FILTER_C_UID_NSO_L = f"(&{FILTER_UID}(|{FILTER_NSROLE} {FILTER_L}))"
FILTER_C_UID_L_UID_NS = f"(|(&{FILTER_UID} {FILTER_L})(&{FILTER_UID} {FILTER_NSROLE}))"
FILTER_C_UID_NS_L = f"(|{FILTER_UID}(&{FILTER_NSROLE} {FILTER_L}))"
FILTER_C_UID_NS_UID_L = f"(&(|{FILTER_UID} {FILTER_NSROLE})(|{FILTER_UID} {FILTER_L}))"
FILTER_C_UID_NSVAD = f"(&{FILTER_UID} {FILTER_NSROLE_VADR})"
FILTER_C_NSVAD_UID = f"(&{FILTER_NSROLE_VADR} {FILTER_UID})"
FILTER_C_NSVADR_UID = f"(|{FILTER_NSROLE_VADR} {FILTER_UID})"
FILTER_C_UID_NS_V = f"(|{FILTER_UID}(&{FILTER_NSROLE} {FILTER_NSROLE_VADR}))"
FILTER_C_UID_NSV = f"(|{FILTER_UID} {FILTER_NSROLE_VADR})"
FILTER_C_NSV_L = f"(|{FILTER_NSROLE_VADR} {FILTER_L_ALL})"
FILTER_C_L_C = f"(&{FILTER_L} {FILTER_C_UID_NS_V})"
FILTER_C_L_C_UID = f"(&(!{FILTER_L})(!{FILTER_C_UID_NS_V}))"
FILTER_C_L_NS = f"(&(!{FILTER_L}){FILTER_C_UID_NS_V})"
FILTER_C_L_C_UIDNS = f"(&{FILTER_L}(!{FILTER_C_UID_NS_V}))"
FILTER_C_L_C_UIDNSV = f"(|{FILTER_L} {FILTER_C_UID_NS_V})"
FILTER_C_N_L_C_UIDNSV = f"(|(!{FILTER_L})(!{FILTER_C_UID_NS_V}))"
FILTER_C_L_C_N_UIDNSV = f"(|(!{FILTER_L}){FILTER_C_UID_NS_V})"
FILTER_C_L_N_C_UIDNSV = f"(|{FILTER_L}(!{FILTER_C_UID_NS_V}))"
FILTER_C_USER1 = f"(&(!{FILTER_UID_USER1}){FILTER_OBJ})"
FILTER_C_USER1_OBJ = f"(|(!{FILTER_UID_USER1}){FILTER_OBJ})"
FILTER_C_NSV_LS = f"(&(!{FILTER_NSROLE_VADR}){FILTER_L_S})"
FILTER_C_N_NSV_LS = f"(|(!{FILTER_NSROLE_VADR}){FILTER_L_S})"
FILTER_C_1_20_30 = f"(&(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})" \
                   f"(!{FILTER_UID_USER30}){FILTER_OBJ})"
FILTER_C_1_20_30_OBJ = f"(|(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})" \
                       f"(!{FILTER_UID_USER30}){FILTER_OBJ})"
FILTER_C_NS_3924 = f"(&(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})" \
                   f"(!{FILTER_RN_4508}){FILTER_L_S})"
FILTER_C_NS_3924_4508 = f"(|(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})" \
                        f"(!{FILTER_RN_4508}){FILTER_L_S})"
FILTER_C_40 = f"(&(!{FILTER_UID_USER40})(&(!{FILTER_UID_USER1})" \
              f"(!{FILTER_UID_USER20})(!{FILTER_UID_USER30}){FILTER_OBJ}))"
FILTER_C_40_USER1 = f"(|(!{FILTER_UID_USER40})(&(!{FILTER_UID_USER1})" \
                    f"(!{FILTER_UID_USER20})(!{FILTER_UID_USER30}){FILTER_OBJ}))"
FILTER_C_2254 = f"(&(!{FILTER_RN_2254}){FILTER_C_NS_3924})"
FILTER_C_2254_3924 = f"(|(!{FILTER_RN_2254}){FILTER_C_NS_3924})"
FILTER_C_N_1_20_30 = f"(&(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})(!{FILTER_UID_USER30}))"
FILTER_C_1_N_20_30 = f"(|(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})(!{FILTER_UID_USER30}))"
FILTER_C_ANV_3924 = f"(&(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_C_ANV_4508 = f"(|(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_C_OBJ_1_20 = f"(& {FILTER_OBJ}(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})" \
                    f"(!{FILTER_UID_USER30}))"
FILTER_C_OBJ_1_20_30 = f"(| {FILTER_OBJ}(!{FILTER_UID_USER1})(!{FILTER_UID_USER20})" \
                       f"(!{FILTER_UID_USER30}))"
FILTER_C_LS_ANV = f"(&{FILTER_L_S}(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_C_LS_NS = f"(|{FILTER_L_S}(!{FILTER_NSROLE_ANOV})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_C_LALL = f"(!(|(!{FILTER_L_ALL})(!{FILTER_L_S})))"
FILTER_C_LAL_LS = f"(|(!{FILTER_L_ALL})(!{FILTER_L_S}))"
FILTER_C_L_EMAIL = f"(&{FILTER_L} {FILTER_EMAIL} {FILTER_C_NSV_L})"
FILTER_C_L_N_EMAIL = f"(&(!{FILTER_L})(!{FILTER_EMAIL})(!{FILTER_C_NSV_L}))"
FILTER_C_L_N_EMAIL_C = f"(&(!{FILTER_L})(!{FILTER_EMAIL}){FILTER_C_NSV_L})"
FILTER_C_L_EMAIL_C = f"(&(!{FILTER_L}){FILTER_EMAIL}(!{FILTER_C_NSV_L}))"
FILTER_C_L_EMAIL_CNS = f"(&(!{FILTER_L}){FILTER_EMAIL} {FILTER_C_NSV_L})"
FILTER_C_L_EMAIL_CNS_L = f"(&{FILTER_L}(!{FILTER_EMAIL})(!{FILTER_C_NSV_L}))"
FILTER_C_L_EMAIL_CNSV_L = f"(&{FILTER_L}(!{FILTER_EMAIL}){FILTER_C_NSV_L})"
FILTER_C_L_N_EMAIL_CNSV_L = f"(&{FILTER_L} {FILTER_EMAIL}(!{FILTER_C_NSV_L}))"
FILTER_C_L_N_EMAIL_ALL = f"(|{FILTER_L} {FILTER_EMAIL} {FILTER_C_NSV_L})"
FILTER_C_L_EMAIL_N_ALL = f"(|(!{FILTER_L})(!{FILTER_EMAIL})(!{FILTER_C_NSV_L}))"
FILTER_C_L_EMAIL_C_ALL = f"(|(!{FILTER_L})(!{FILTER_EMAIL}){FILTER_C_NSV_L})"
FILTER_C_EMAIL_C_ALL = f"(|(!{FILTER_L}){FILTER_EMAIL}(!{FILTER_C_NSV_L}))"
FILTER_C_L_N_EMAIL_C_ALL = f"(|(!{FILTER_L}){FILTER_EMAIL} {FILTER_C_NSV_L})"
FILTER_L_N_EMAIL_C_ALL = f"(|{FILTER_L}(!{FILTER_EMAIL})(!{FILTER_C_NSV_L}))"
FILTER_L_E_C = f"(|{FILTER_L}(!{FILTER_EMAIL}){FILTER_C_NSV_L})"
FILTER_L_E_N_C = f"(|{FILTER_L} {FILTER_EMAIL}(!{FILTER_C_NSV_L}))"


VALUES = [FILTER_POSTAL, FILTER_ADDRESS, FILTER_8888, FILTER_6666,
          FILTER_VPE, FILTER_EMAIL, FILTER_EMAILQUATA, FILTER_QUATA,
          FILTER_ROLE, FILTER_POST, FILTER_CLASS, FILTER_CLASSES,
          FILTER_AAA, FILTER_VE, FILTER_VPEM, FILTER_900, FILTER_MAIL_600,
          FILTER_MAIL_600_G, FILTER_NS_NEW, FILTER_WAL, FILTER_MW, FILTER_RN,
          FILTER_L, FILTER_USER1, FILTER_OBJECT, FILTER_L_S, FILTER_RN_3924, FILTER_L_ALL,
          FILTER_OBJ_ALL, FILTER_MAIL_900, FILTER_MAIL_100_L, FILTER_MAIL_E_600, FILTER_MAIL_E_900,
          FILTER_MAIL_EGE_900, FILTER_MAIL_FR_EGE_900, FILTER_MAIL_EGE_600, FILTER_MAIL_FR_EGE_600,
          FILTER_MAIL_E_600_2, FILTER_MAIL_EGE_100, FILTER_MAIL_FR_EGE_100, FILTER_MAIL_E_100,
          FILTER_C_N_NS_VAD, FILTER_C_UID_NS, FILTER_C_N_UID_NS,
          FILTER_C_N_UID_NSO, FILTER_C_UID_N_NSO, FILTER_C_UID_NSO_L,
          FILTER_C_UID_L_UID_NS, FILTER_C_UID_NS_L, FILTER_C_UID_NS_UID_L,
          FILTER_C_NSVADR_UID, FILTER_C_UID_NS_V, FILTER_C_UID_NSV,
          FILTER_C_NSV_L, FILTER_C_L_C, FILTER_C_L_C_UID, FILTER_C_L_NS,
          FILTER_C_L_C_UIDNS, FILTER_C_L_C_UIDNSV, FILTER_C_N_L_C_UIDNSV,
          FILTER_C_L_C_N_UIDNSV, FILTER_C_L_N_C_UIDNSV, FILTER_C_USER1,
          FILTER_C_USER1_OBJ, FILTER_C_NSV_LS, FILTER_C_N_NSV_LS,
          FILTER_C_1_20_30, FILTER_C_1_20_30_OBJ, FILTER_C_NS_3924,
          FILTER_C_NS_3924_4508, FILTER_C_40, FILTER_C_40_USER1,
          FILTER_C_2254, FILTER_C_2254_3924, FILTER_C_N_1_20_30, FILTER_C_1_N_20_30,
          FILTER_C_ANV_3924, FILTER_C_ANV_4508, FILTER_C_OBJ_1_20,
          FILTER_C_OBJ_1_20_30, FILTER_C_LS_ANV,
          FILTER_C_LS_NS, FILTER_C_LALL, FILTER_C_LAL_LS, FILTER_C_L_EMAIL,
          FILTER_C_L_N_EMAIL, FILTER_C_L_N_EMAIL_C, FILTER_C_L_EMAIL_CNS,
          FILTER_C_L_EMAIL_CNSV_L, FILTER_C_L_N_EMAIL_ALL, FILTER_C_L_EMAIL_N_ALL,
          FILTER_C_L_EMAIL_C_ALL, FILTER_C_EMAIL_C_ALL, FILTER_C_L_N_EMAIL_C_ALL,
          FILTER_L_N_EMAIL_C_ALL, FILTER_L_E_C, FILTER_L_E_N_C]

VALUES_NEGATIVE = [FILTER_7777, FILTER_FRED, FILTER_ECLASS, FILTER_ECLASS_1,
                   FILTER_ECLASS_2, FILTER_ECLASS_3, FILTER_ECLASS_4, FILTER_VPE_1,
                   FILTER_VPE_2, FILTER_MAILQUATA_1, FILTER_MAILQUATA_2, FILTER_MAIL_100,
                   FILTER_MAIL_900_G, FILTER_NS_MANAGE, FILTER_VADDER,
                   FILTER_ANOTHER, FILTER_NS_VADD, FILTER_USER20, FILTER_USER30, FILTER_CN,
                   FILTER_RN_4508, FILTER_USER40, FILTER_RN_2254, FILTER_MAIL_100_EL,
                   FILTER_MAIL_FR, FILTER_MAIL_100_E, FILTER_MAIL_600_ELE, FILTER_MAIL_FR_600,
                   FILTER_MAIL_ELE_900, FILTER_MAIL_FR_ELE_900, FILTER_MAIL_E_900_2,
                   FILTER_ATTRIBUTE, FILTER_DES, FILTER_C_NS_VAD, FILTER_C_O_N_NS_VAD,
                   FILTER_C_N_NS_O_VAD, FILTER_C_UID_NSVAD, FILTER_C_NSVAD_UID,
                   FILTER_C_L_EMAIL_C, FILTER_C_L_EMAIL_CNS_L, FILTER_C_L_N_EMAIL_CNSV_L]


def non_english_user(people, user, cn_cn, ou_ou, des, tele, facetele, be_be, lang):
    """
    Will create users with non english name
    """
    people.create(properties={
        'uid': user,
        'cn': cn_cn,
        'sn': cn_cn.split()[1].title(),
        'givenname': cn_cn.split()[0],
        'ou': ou_ou,
        'mail': f'{user}@anujborah.com',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + user,
        'telephoneNumber': tele,
        'facsimileTelephoneNumber': facetele,
        f'cn;lang-{be_be}': lang,
        f'sn;lang-{be_be}': lang.split()[1],
        f'givenName;lang-{be_be}': lang.split()[0],
        'userpassword': PW_DM,
        'manager': 'uid=' + user + ',ou=' + ou_ou + ',' + DEFAULT_SUFFIX,
        'description': des
    })


def english_named_user(people, user, org, l_l, telephone, facsimile_telephone_number, rn_rn):
    """
    Will create users with English name
    """
    people.create(properties={
        'uid': user,
        'cn': user,
        'sn': user,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + user,
        'givenname': user,
        'ou': org,
        'mail': f'{user}@example.com',
        'roomnumber': rn_rn,
        'l': l_l,
        'telephonenumber': telephone,
        'manager': f'uid={user},ou=People,{DEFAULT_SUFFIX}',
        'facsimiletelephonenumber': facsimile_telephone_number,
        'userpassword': PW_DM,
    })


def user_with_postal_code(people, user, address, pin):
    """
    Will create users with postal Address
    """
    people.create(properties={
        'uid': user,
        'cn': user,
        'sn': user,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + user,
        'postalAddress': address,
        'postalCode': pin,
    })


@pytest.fixture(scope="module")
def _create_test_entries(topo):
    # Changing schema
    current_schema = Schema(topo.standalone)
    current_schema.add('attributetypes',
                       "( 9.9.8.4 NAME 'emailclass' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 "
                       "X-ORIGIN 'RFC 2256' )")
    current_schema.add('objectclasses',
                       "( 9.9.8.2 NAME 'mailSchemeUser' DESC 'User Defined ObjectClass' "
                       "SUP 'top' MUST ( objectclass )  "
                       "MAY (aci $ emailclass) X-ORIGIN 'RFC 2256' )")

    # Creating ous
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    for ou_ou in ['Çéliné Ändrè', 'Ännheimè', 'Çlose Crèkä',
                  'Sàn Fråncêscô', 'Netscape Servers',
                  'COS', ]:
        ous.create(properties={'ou': ou_ou})

    ous_mail = OrganizationalUnits(topo.standalone, f'ou=COS,{DEFAULT_SUFFIX}')
    ous_mail.create(properties={'ou': 'MailSchemeClasses'})

    # Creating users
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for user, org, l_l, telephone, facetele, rn_rn in [
            ['scarter', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '4612'],
            ['tmorris', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 9187', '+1 408 555 8473', '4117'],
            ['kvaughan', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 5625', ' +1 408 555 3372', '2871'],
            ['abergin', ['Product Testing', 'People'], 'Cupertino',
             '+1 408 555 8585', '+1 408 555 7472', '3472'],
            ['dmiller', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 9423', '+1 408 555 0111', '4135'],
            ['gfarmer', ['Accounting', 'People'], 'Cupertino',
             '+1 408 555 6201', '+1 408 555 8473', '1269'],
            ['kwinters', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 9069', '+1 408 555 1992', '4178'],
            ['trigden', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 9280', '+1 408 555 8473', '3584'],
            ['cschmith', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 8011', '+1 408 555 4774', '0416'],
            ['jwallace', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 0319', '+1 408 555 8473', '1033'],
            ['jwalker', ['Product Testing', 'People'], 'Cupertino',
             '+1 408 555 1476', '+1 408 555 1992', '3915'],
            ['tclow', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 8825', '+1 408 555 1992', '4376'],
            ['rdaugherty', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 1296', '+1 408 555 1992', '0194'],
            ['jreuter', ['Product Testing', 'People'], 'Cupertino',
             '+1 408 555 1122', '+1 408 555 8721', '2942'],
            ['tmason', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 1596', '+1 408 555 9751', '1124'],
            ['bhall', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '4612'],
            ['btalbot', ['Human Resources', 'People'], 'Cupertino',
             '+1 408 555 6067', '+1 408 555 9751', '3532'],
            ['mward', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '1707'],
            ['bjablons', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 6067', '+1 408 555 9751', '0906'],
            ['jmcFarla', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '2359'],
            ['llabonte', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '2854'],
            ['jcampaig', ['Product Development', 'People'], 'Cupertino',
             '+1 408 555 6067', '+1 408 555 9751', '4385'],
            ['bhal2', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 6067', '+1 408 555 9751', '2758'],
            ['alutz', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '1327'],
            ['btalbo2', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '1205'],
            ['achassin', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '0466'],
            ['hmiller', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '4304'],
            ['jcampai2', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '1377'],
            ['lulrich', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 6067', '+1 408 555 9751', '0985'],
            ['mlangdon', ['Product Development', 'People'], 'Cupertino',
             '+1 408 555 6067', '+1 408 555 9751', '4471'],
            ['striplet', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '3083'],
            ['gtriplet', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 6067', '+1 408 555 9751', '4023'],
            ['jfalena', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '1917'],
            ['speterso', ['Human Resources', 'People'], 'Cupertino',
             '+1 408 555 6067', '+1 408 555 9751', '3073'],
            ['ejohnson', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '3737'],
            ['prigden', ['Accounting', 'People'], 'Santa',
             '+1 408 555 6067', '+1 408 555 9751', '1271'],
            ['bwalker', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 6067', '+1 408 555 9751', '3529'],
            ['kjensen', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '1944'],
            ['mlott', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '0498'],
            ['cwallace', ['Product Development', 'People'], 'Cupertino',
             '+1 408 555 4798', '+1 408 555 9751', '0349'],
            ['falbers', ['Accounting', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '1439'],
            ['calexand', ['Product Development', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '2884'],
            ['phunt', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '1183'],
            ['awhite', ['Product Testing', 'People'], 'Sunnyvale',
             '+1 408 555 4798', '+1 408 555 9751', '0142'],
            ['sfarmer', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '0019'],
            ['jrentz', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '3025'],
            ['ahall', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '3050'],
            ['lstockto', ['Product Testing', 'People'], 'Santa Clara',
             '+1 408 555 0518', '+1 408 555 4774', '0169'],
            ['ttully', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 2274', '+1 408 555 0111', '3924'],
            ['polfield', ['Human Resources', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '1376'],
            ['scarte2', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 4798', '+1 408 555 9751', '2013'],
            ['tkelly', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 4295', '+1 408 555 1992', '3107'],
            ['mmcinnis', ['Product Development', 'People'], 'Santa Clara',
             '+1 408 555 9655', '+1 408 555 8721', '4818'],
            ['brigden', ['Human Resources', 'People'], 'Sunnyvale',
             '+1 408 555 9655', '+1 408 555 8721', '1643'],
            ['mtyler', ['Human Resources', 'People'], 'Cupertino',
             '+1 408 555 9655', '+1 408 555 8721', '2701'],
            ['rjense2', ['Product Testing', 'People'], 'Sunnyvale',
             '+1 408 555 9655', '+1 408 555 8721', '1984'],
            ['rhunt', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 9655', '+1 408 555 8721', '0718'],
            ['ptyler', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 9655', '+1 408 555 8721', '0327'],
            ['gtyler', ['Accounting', 'People'], 'Santa Clara',
             '+1 408 555 9655', '+1 408 555 8721', '0312']]:
        english_named_user(users_people, user, org, l_l, telephone, facetele, rn_rn)

    # Creating Users
    users_annahame = UserAccounts(topo.standalone, f'ou=Ännheimè,{DEFAULT_SUFFIX}', rdn=None)
    users_sanfran = UserAccounts(topo.standalone, f'ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', rdn=None)
    users_andre = UserAccounts(topo.standalone, f'ou=Çéliné Ändrè,{DEFAULT_SUFFIX}', rdn=None)
    users_close = UserAccounts(topo.standalone, f'ou=Çlose Crèkä,{DEFAULT_SUFFIX}', rdn=None)
    for people, user, cn_cn, ou_ou, des, tele, facetele, be_be, lang in [
            [users_annahame, 'user0', 'Babette Ryndérs', 'Ännheimè',
             'This is Babette Ryndérs description', '+1 415 788-4115',
             '+1 804 849-2367', 'es', 'Babette Ryndérs'],
            [users_sanfran, 'user1', 'mÿrty DeCoùrsin', 'Sàn Fråncêscô',
             'This is mÿrty DeCoùrsins description', '+1 408 689-8883',
             '+1 804 849-2367', 'ie', 'mÿrty DeCoùrsin'],
            [users_sanfran, 'user3', 'Kéñnon Fùndérbùrg', 'Sàn Fråncêscô',
             "This is Kéñnon Fùndérbùrg's description", '+1 408 689-8883',
             '+1 804 849-2367', 'it', 'Kéñnon Fùndérbùrg'],
            [users_sanfran, 'user5', 'Dàsya Cozàrt', 'Sàn Fråncêscô',
             "This is Dàsya Cozàrt's description", '+1 408 689-8883',
             '+1 804 849-2367', 'be', 'Dàsya Cozàrt'],
            [users_andre, 'user2', "Rôw O'Connér", 'Çéliné Ändrè',
             "This is Rôw O'Connér's description", '+1 408 689-8883',
             '+1 804 849-2367', 'it', "Rôw O'Connér"],
            [users_andre, 'user4', 'Theadora Ebérle', 'Çéliné Ändrè',
             "This is Kéñnon Fùndérbùrg's description", '+1 408 689-8883',
             '+1 804 849-2367', 'de', 'Theadora Ebérle'],
            [users_andre, 'user6', 'mÿrv Callânân', 'Çéliné Ändrè',
             "This is mÿrv Callânân's description", '+1 408 689-8883',
             '+1 804 849-2367', 'fr', 'mÿrv Callânân'],
            [users_close, 'user7', 'Ñäthan Ovâns', 'Çlose Crèkä',
             "This is Ñäthan Ovâns's description", '+1 408 689-8883',
             '+1 804 849-2367', 'be', 'Ñäthan Ovâns']]:
        non_english_user(people, user, cn_cn, ou_ou, des, tele, facetele, be_be, lang)

    # Creating User Entry
    for user, address, pin in [
            ['Secretary1', '123 Castro St., Mountain View, CA', '99999'],
            ['Secretary2', '234 Ellis St., Mountain View, CA', '88888'],
            ['Secretary3', '345 California Av., Mountain View, CA', '77777'],
            ['Secretary4', '456 Villa St., Mountain View, CA', '66666'],
            ['Secretary5', '567 University Av., Mountain View, CA', '55555']]:
        user_with_postal_code(users_people, user, address, pin)

    # Adding properties to mtyler
    mtyler = UserAccount(topo.standalone, 'uid=mtyler, ou=people, dc=example, dc=com')
    for value1, value2 in [
            ('objectclass', ['mailSchemeUser', 'mailRecipient']),
            ('emailclass', 'vpemail'),
            ('mailquota', '600'),
            ('multiLineDescription', 'fromentry This is the special \2a attribute value')]:
        mtyler.add(value1, value2)

    # Adding properties to rjense2
    rjense2 = UserAccount(topo.standalone, 'uid=rjense2, ou=people, dc=example, dc=com')
    for value1, value2 in [
            ('objectclass', ['mailRecipient', 'mailSchemeUser']),
            ('emailclass', 'vpemail')]:
        rjense2.add(value1, value2)

    # Creating managed role
    ManagedRoles(topo.standalone, DEFAULT_SUFFIX).create(properties={
        'description': 'This is the new managed role configuration',
        'cn': 'new managed role'})

    # Creating filter role
    filters = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
    filters.create(properties={
        'nsRoleFilter': '(uid=*wal*)',
        'description': 'this is the new filtered role',
        'cn': 'new filtered role'})
    filters.create(properties={
        'nsRoleFilter': '(&(postalCode=77777)(uid=*er*))',
        'description': 'This is the new vddr filter role config',
        'cn': 'new vaddr filtered role'
    })
    filters.create(properties={
        'nsRoleFilter': '(&(postalCode=66666)(l=Cupertino))',
        'description': 'This is the new vddr filter role config',
        'cn': 'another vaddr role'
    })


@pytest.mark.parametrize("real_value", VALUES)
def test_param_positive(topo, _create_test_entries, real_value):
    """
    Will test Filters with positive output.

    :id: 71de14a4-9f22-11e8-b5cc-8c16451d917b
    :parametrized: yes
    :setup: Standalone Server
    :steps:
        1. Create Filter rules.
        2. Try to pass filter rules as per the condition .
    :expectedresults:
        1. It should pass
        2. It should pass
    """
    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(real_value)


@pytest.mark.parametrize("real_value", VALUES_NEGATIVE)
def test_param_negative(topo, _create_test_entries, real_value):
    """
    Will test Filetrs with 0 outputs

    :id: 81054e7a-9f22-11e8-a461-8c16451d917b
    :parametrized: yes
    :setup: Standalone Server
    :steps:
        1. Create Filter rules.
        2. Try to pass filter rules as per the condition .
        3. All Filters will give 0 output as these are negative test cases.
    :expectedresults:
        1. It should pass
        2. It should pass
        3. no output
    """
    assert not Accounts(topo.standalone, DEFAULT_SUFFIX).filter(real_value)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

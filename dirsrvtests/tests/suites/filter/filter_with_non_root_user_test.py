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
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier1

FILTER_MWARD = "(uid=mward)"
FILTER_L = "(l=sunnyvale)"
FILTER_MAIL = "(mail=jreu*)"
FILTER_EXAM = "(mail=*exam*)"
FILTER_7393 = "(telephonenumber=*7393)"
FILTER_408 = "(telephonenumber=*408*3)"
FILTER_UID = "(uid=*)"
FILTER_PASSWD = "(userpassword=*)"
FILTER_FRED = "(fred=*)"
FILTER_AAA = "(uid:2.16.840.1.113730.3.3.2.15.1:=>AAA)"
FILTER_AAA_ES = "(uid:es:=>AAA)"
FILTER_AAA_UID = "(uid:2.16.840.1.113730.3.3.2.15.1.5:=AAA)"
FILTER_100 = "(uid:2.16.840.1.113730.3.3.2.15.1:=>user100)"
FILTER_ES_100 = "(uid:es:=>user100)"
FILTER_UID_100 = "(uid:2.16.840.1.113730.3.3.2.15.1.5:=user100)"
FILTER_UID_1 = "(uid:2.16.840.1.113730.3.3.2.15.1:=<1)"
FILTER_UID_ES = "(uid:es:=<1)"
FILTER_UID_2 = "(uid:2.16.840.1.113730.3.3.2.15.1.1:=1)"
FILTER_UID_USER1 = "(uid:2.16.840.1.113730.3.3.2.15.1:=<user1)"
FILTER_ES_USER1 = "(uid:es:=<user1)"
FILTER_UI_USER1 = "(uid:2.16.840.1.113730.3.3.2.15.1.1:=user1)"
FILTER_Z = "(uid:2.16.840.1.113730.3.3.2.15.1:=<z)"
FILTER_NZ = "(uid:es:=<z)"
FILTER_UIDZ = "(uid:2.16.840.1.113730.3.3.2.15.1.1:=z)"
FILTER_UID_LS = "(uid<=1)"
FILTER_UID_LA = "(uid<=A)"
FILTER_USER1 = "(uid=user1)"
FILTER_UIDLEZ = "(uid<=Z)"
FILTER_UIDGE1 = "(uid>=1)"
FILTER_UIDGEA = "(uid>=A)"
FILTER_UIDGEAU20 = "(uid>=user20)"
FILTER_UIDGEZ = "(uid>=Z)"
FILTER_A = "(uid:2.16.840.1.113730.3.3.2.18.1:=<=A)"
FILTER_FR_A = "(uid:fr:=<=A)"
FILTER_E_A = "(uid:2.16.840.1.113730.3.3.2.18.1.2:=A)"
FILTER_USER20 = "(uid:2.16.840.1.113730.3.3.2.18.1:=<=user20)"
FILTER_L_USER20 = "(uid:fr:=<=user20)"
FILTER_E_USER20 = "(uid:2.16.840.1.113730.3.3.2.18.1.2:=user20)"
FILTER_Z2 = "(uid:2.16.840.1.113730.3.3.2.18.1:=<=z)"
FILTER_LE_Z = "(uid:fr:=<=z)"
FILTER_E_Z = "(uid:2.16.840.1.113730.3.3.2.18.1.2:=z)"
FILTER_GE_Z = "(uid:2.16.840.1.113730.3.3.2.18.1:=>=A)"
FILTER_GE_A = "(uid:fr:=>=A)"
FILTER_UID_A = "(uid:2.16.840.1.113730.3.3.2.18.1.4:=A)"
FILTER_UID_USER20 = "(uid:2.16.840.1.113730.3.3.2.18.1:=>=user20)"
FILTER_FR_USER20 = "(uid:fr:=>=user20)"
FILTER_UID_E_USER20 = "(uid:2.16.840.1.113730.3.3.2.18.1.4:=user20)"
FILTER_EGE_Z = "(uid:2.16.840.1.113730.3.3.2.18.1:=>=z)"
FILTER_FR_Z = "(uid:fr:=>=z)"
FILTER_UID_Z = "(uid:2.16.840.1.113730.3.3.2.18.1.4:=z)"
FILTER_SN = "(sn~=tiller)"
FILTER_GN = "(givenName~=pricella)"
FILTER_DES = "(description=This is the special * attribute value)"
FILTER_DES_X = "(description=*x*)"
FILTER_PTYL = "(uid=ptyler)"
FILTER_WAL = "(uid=*wal*)"
FILTER_RN = "(roomNumber=0312)"
FILTER_MW = "(uid=mw*)"
FILTER_2295 = "(roomNumber=2295)"
FILTER_CAPERTION = "(l=Cupertino)"
FILTER_INTER = "(objectclass=inetorgperson)"
FILTER_MAIL2 = "(mail=cnewport@example.com)"
FILTER_VALE = "(l=sunnyvale)"
FILTER_UID20 = "(uid=user20)"
FILTER_UID30 = "(uid=user30)"
FILTER_4012 = "(roomNumber=200)"
FILTER_3924 = "(roomNumber=201)"
FILTER_4508 = "(roomNumber=202)"
FILTER_UID40 = "(uid=user40)"
FILTER_2254 = "(roomNumber=2254)"
FILTER_L2 = "(l=*)"
FILTER_C_SN_GN = f"(&{FILTER_SN} {FILTER_GN})"
FILTER_C_SN_PTYL = f"(&(!{FILTER_SN})(!{FILTER_PTYL}))"
FILTER_SN_PTYL = f"(&(!{FILTER_SN}) {FILTER_PTYL})"
FILTER_N_SN_PTYL = f"(&{FILTER_SN}(!{FILTER_PTYL}))"
FILTER_C_WALL_RN = f"(|{FILTER_WAL} {FILTER_RN})"
FILTER_N_WALL_RN = f"(|(!{FILTER_WAL})(!{FILTER_RN}))"
FILTER_C_N_WALL_RN = f"(|(!{FILTER_WAL}){FILTER_RN})"
FILTER_C_N_WAL_RN = f"(|{FILTER_WAL}(!{FILTER_RN}))"
FILTER_C_WAL_SN = f"(&{FILTER_WAL}(|{FILTER_SN} {FILTER_2295}))"
FILTER_C_WAL_2295 = f"(|(&{FILTER_WAL} {FILTER_2295})(&{FILTER_WAL} {FILTER_SN}))"
FILTER_C_WAL_SN_2295 = f"(|{FILTER_WAL}(&{FILTER_SN} {FILTER_2295}))"
FILTER_C_WAL_SN_WAL = f"(&(|{FILTER_WAL} {FILTER_SN})(|{FILTER_WAL} {FILTER_2295}))"
FILTER_WAL_2295 = f"(&{FILTER_WAL} {FILTER_2295})"
FILTER_2295_WAL = f"(&{FILTER_2295} {FILTER_WAL})"
FILTER_OR_2295_WAL = f"(|{FILTER_2295} {FILTER_WAL})"
FILTER_OR_WAL_SN = f"(|{FILTER_WAL}(&{FILTER_SN} {FILTER_2295}))"
FILTER_OR_WAL_2295 = f"(|{FILTER_WAL} {FILTER_2295})"
FILTER_OR_WAL_L = f"(|{FILTER_WAL} {FILTER_L2})"
FILTER_AND_C_OR = f"(&{FILTER_CAPERTION} {FILTER_OR_WAL_SN})"
FILTER_AND_C_F = f"(&(!{FILTER_CAPERTION})(!{FILTER_OR_WAL_SN}))"
FILTER_AND_C_W_SN = f"(&(!{FILTER_CAPERTION}){FILTER_OR_WAL_SN})"
FILTER_AND_N_C_W_SN = f"(&{FILTER_CAPERTION}(!{FILTER_OR_WAL_SN}))"
FILTER_OR_N_C_W_SN = f"(|{FILTER_CAPERTION} {FILTER_OR_WAL_SN})"
FILTER_OR_N_CWS = f"(|(!{FILTER_CAPERTION})(!{FILTER_OR_WAL_SN}))"
FILTER_OR_N_CWSN = f"(|(!{FILTER_CAPERTION}){FILTER_OR_WAL_SN})"
FILTER_OR_CWSN_N = f"(|{FILTER_CAPERTION}(!{FILTER_OR_WAL_SN}))"
FILTER_AND_USER1 = f"(&(!{FILTER_USER1}){FILTER_INTER})"
FILTER_OR_USER1 = f"(|(!{FILTER_USER1}){FILTER_INTER})"
FILTER_MAIL_VAL = f"(&(!{FILTER_MAIL2}){FILTER_VALE})"
FILTER_OR_MAIL_VAL = f"(|(!{FILTER_MAIL2}){FILTER_VALE})"
FILTER_USER1_UID = f"(&(!{FILTER_USER1})(!{FILTER_UID20})(!{FILTER_UID30}){FILTER_INTER})"
FILTER_USER1_UID20 = f"(|(!{FILTER_USER1})(!{FILTER_UID20})(!{FILTER_UID30}){FILTER_INTER})"
FILTER_USER4012_3924 = f"(&(!{FILTER_4012})(!{FILTER_3924})(!{FILTER_4508}){FILTER_VALE})"
FILTER_USER4012_3924_4520 = f"(|(!{FILTER_4012})(!{FILTER_3924})(!{FILTER_4508}){FILTER_VALE})"
FILTER_USER40_USER1 = f"(&(!{FILTER_UID40})(&(!{FILTER_USER1})(!{FILTER_UID20})" \
                      f"(!{FILTER_UID30}){FILTER_INTER}))"
FILTER_USER40_USER20 = f"(|(!{FILTER_UID40})(&(!{FILTER_USER1})(!{FILTER_UID20})" \
                       f"(!{FILTER_UID30}){FILTER_INTER}))"
FILTER_SN0 = f"(&(!{FILTER_2254}){FILTER_USER4012_3924})"
FILTER_SN1 = f"(|(!{FILTER_2254}){FILTER_USER4012_3924})"
FILTER_ORG = "(objectclass=inetorgperson)"
FILTER_SV = "(l=sunnyvale)"
FILTER_USER30 = "(uid=user30)"
FILTER_RN_4012 = "(roomNumber=4012)"
FILTER_RN_3924 = "(roomNumber=3924)"
FILTER_RN_4508 = "(roomNumber=4508)"
FILTER_L_ALL = "(l=*)"
FILTER_UID_WAL = f"(|(uid=*wal*) {FILTER_L_ALL})"
FILTER_U1_U20_U30 = f"(&(!{FILTER_USER1})(!{FILTER_USER20})(!{FILTER_USER30}))"
FILTER_N_U1_U20_U30 = f"(|(!{FILTER_USER1})(!{FILTER_USER20})(!{FILTER_USER30}))"
FILTER_RN_4012_3924_4508 = f"(&(!{FILTER_RN_4012})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_RN_N_4012_3924_4508 = f"(|(!{FILTER_RN_4012})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_RN_ORG_1_20 = f"(& {FILTER_ORG}(!{FILTER_USER1})(!{FILTER_USER20})(!{FILTER_USER30}))"
FILTER_RN_ORG_1_20_30 = f"(| {FILTER_ORG}(!{FILTER_USER1})(!{FILTER_USER20})(!{FILTER_USER30}))"
FILTER_SV_4012_3924 = f"(&{FILTER_SV}(!{FILTER_RN_4012})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_SV_4012_3924_45 = f"(|{FILTER_SV}(!{FILTER_RN_4012})(!{FILTER_RN_3924})(!{FILTER_RN_4508}))"
FILTER_ALL_SV = f"(!(|(!{FILTER_L_ALL})(!{FILTER_SV})))"
FILTER_L_ALL_SV = f"(|(!{FILTER_L_ALL})(!{FILTER_SV}))"
FILTER_CAP_EXAM = f"(&{FILTER_CAPERTION} {FILTER_EXAM} {FILTER_UID_WAL})"
FILTER_CAP_EXAM_WALL = f"(&(!{FILTER_CAPERTION})(!{FILTER_EXAM})(!{FILTER_UID_WAL}))"
FILTER_CAP_EXAM_U_WALL = f"(&(!{FILTER_CAPERTION})(!{FILTER_EXAM}){FILTER_UID_WAL})"
FILTER_EXAM_U_WALL = f"(&(!{FILTER_CAPERTION}){FILTER_EXAM}(!{FILTER_UID_WAL}))"
FILTER_CAP_E_W = f"(&(!{FILTER_CAPERTION}){FILTER_EXAM} {FILTER_UID_WAL})"
FILTER_CAP_E_N_W = f"(&{FILTER_CAPERTION}(!{FILTER_EXAM})(!{FILTER_UID_WAL}))"
FILTER_CAP_N_E_W = f"(&{FILTER_CAPERTION}(!{FILTER_EXAM}){FILTER_UID_WAL})"
FILTER_N_CP_E_W = f"(&{FILTER_CAPERTION} {FILTER_EXAM}(!{FILTER_UID_WAL}))"
FILTER_N_CP_N_E_W = f"(|{FILTER_CAPERTION} {FILTER_EXAM} {FILTER_UID_WAL})"
FILTER_N_CP_N_E_N_W = f"(|(!{FILTER_CAPERTION})(!{FILTER_EXAM})(!{FILTER_UID_WAL}))"
FILTER_OR_CP_E_W = f"(|(!{FILTER_CAPERTION})(!{FILTER_EXAM}){FILTER_UID_WAL})"
FILTER_OR_N_CP_E_W = f"(|(!{FILTER_CAPERTION}){FILTER_EXAM}(!{FILTER_UID_WAL}))"
FILTER_OR_N_CP_N_E_W = f"(|(!{FILTER_CAPERTION}){FILTER_EXAM} {FILTER_UID_WAL})"
FILTER_OR_N_CP_N_E_N_W = f"(|{FILTER_CAPERTION}(!{FILTER_EXAM})(!{FILTER_UID_WAL}))"
FILTER_NOT_CP_N_E_N_W = f"(|{FILTER_CAPERTION}(!{FILTER_EXAM}){FILTER_UID_WAL})"
FILTER_NOT_CP_NOT_E_N_W = f"(|{FILTER_CAPERTION} {FILTER_EXAM}(!{FILTER_UID_WAL}))"


VALUES = [FILTER_7393, FILTER_408]

POSITIVE = [FILTER_MWARD, FILTER_L, FILTER_MAIL, FILTER_EXAM, FILTER_UID,
            FILTER_AAA, FILTER_AAA_ES, FILTER_AAA_UID, FILTER_100,
            FILTER_ES_100, FILTER_UID_100, FILTER_UI_USER1, FILTER_UIDZ,
            FILTER_USER1, FILTER_UIDLEZ, FILTER_UIDGE1, FILTER_UIDGEA, FILTER_UIDGEAU20,
            FILTER_E_USER20, FILTER_E_Z, FILTER_GE_Z, FILTER_GE_A, FILTER_UID_A,
            FILTER_UID_USER20, FILTER_FR_USER20, FILTER_UID_E_USER20, FILTER_EGE_Z,
            FILTER_FR_Z, FILTER_DES, FILTER_DES_X, FILTER_PTYL, FILTER_WAL, FILTER_RN,
            FILTER_MW, FILTER_2295, FILTER_CAPERTION, FILTER_INTER, FILTER_VALE, FILTER_4012,
            FILTER_3924, FILTER_4508, FILTER_L2, FILTER_C_SN_PTYL, FILTER_SN_PTYL,
            FILTER_C_WALL_RN, FILTER_N_WALL_RN, FILTER_C_N_WALL_RN,
            FILTER_C_N_WAL_RN, FILTER_C_WAL_SN, FILTER_C_WAL_2295, FILTER_C_WAL_SN_2295,
            FILTER_C_WAL_SN_WAL, FILTER_WAL_2295, FILTER_2295_WAL, FILTER_OR_2295_WAL,
            FILTER_OR_WAL_SN, FILTER_OR_WAL_2295, FILTER_OR_WAL_L, FILTER_AND_C_OR,
            FILTER_AND_C_F, FILTER_AND_C_W_SN, FILTER_AND_N_C_W_SN, FILTER_OR_N_C_W_SN,
            FILTER_OR_N_CWS, FILTER_OR_N_CWSN, FILTER_OR_CWSN_N, FILTER_AND_USER1,
            FILTER_OR_USER1, FILTER_MAIL_VAL, FILTER_C_WAL_SN_WAL, FILTER_WAL_2295,
            FILTER_2295_WAL, FILTER_OR_2295_WAL, FILTER_USER4012_3924_4520, FILTER_USER40_USER1,
            FILTER_USER40_USER20, FILTER_SN0, FILTER_SN1, FILTER_U1_U20_U30, FILTER_N_U1_U20_U30,
            FILTER_RN_4012_3924_4508, FILTER_RN_N_4012_3924_4508, FILTER_RN_ORG_1_20,
            FILTER_RN_ORG_1_20_30, FILTER_SV_4012_3924, FILTER_SV_4012_3924_45, FILTER_ALL_SV,
            FILTER_L_ALL_SV, FILTER_CAP_EXAM_WALL, FILTER_CAP_EXAM_U_WALL,
            FILTER_CAP_E_W, FILTER_N_CP_N_E_W, FILTER_N_CP_N_E_N_W, FILTER_OR_CP_E_W,
            FILTER_OR_N_CP_E_W, FILTER_OR_N_CP_N_E_W, FILTER_OR_N_CP_N_E_N_W,
            FILTER_NOT_CP_N_E_N_W, FILTER_NOT_CP_NOT_E_N_W, FILTER_CAP_N_E_W]

NEGATIVE = [FILTER_PASSWD, FILTER_FRED, FILTER_UID_1, FILTER_UID_ES, FILTER_UID_2,
            FILTER_UID_USER1, FILTER_ES_USER1, FILTER_Z, FILTER_NZ, FILTER_UID_LS,
            FILTER_UID_LA, FILTER_UIDGEZ, FILTER_A, FILTER_FR_A, FILTER_E_A,
            FILTER_USER20, FILTER_L_USER20, FILTER_Z2,
            FILTER_LE_Z, FILTER_UID_Z, FILTER_SN, FILTER_GN, FILTER_MAIL2, FILTER_UID20,
            FILTER_UID30, FILTER_UID40, FILTER_C_SN_GN, FILTER_N_SN_PTYL, FILTER_EXAM_U_WALL,
            FILTER_CAP_E_N_W, FILTER_N_CP_E_W, FILTER_CAP_EXAM]


def create_users_all(instance, user, room, l_l, description, telephonenumber):
    """
    Will create users with different type of l
    """
    instance.create(properties={
        'mail': f'{user}@redhat.com',
        'uid': user,
        'givenName': user.title(),
        'cn': f'bit {user}',
        'sn': user.title(),
        'l': l_l,
        'manager': f'uid={user},ou=People,{DEFAULT_SUFFIX}',
        'roomnumber': room,
        'userpassword': PW_DM,
        'homeDirectory': '/home/' + user,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'description': description,
        'telephonenumber': telephonenumber
    })


@pytest.fixture(scope="module")
def _create_entries(topo):
    """
    Will create necessary users for this script.
    """

    # Add anonymous aci
    ANON_ACI = "(targetattr != \"userpassword\")(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare) userdn = \"ldap:///anyone\";)"
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', ANON_ACI)

    # Creating Users
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for user, room in [('scarte2', '2013'),
                       ('mward', '1707'),
                       ('tclow', '4376'),
                       ('bwalker', '3529')]:
        create_users_all(users_people, user, room, 'Santa Clara',
                         'This is the special * attribute value',
                         '+1 408 555 7393')

    for number in range(200, 300):
        create_users_all(users_people, f'user{number}', f'{number}',
                         'Sunnyvale', 'Not the one you looking for.',
                         '123')

    for user, room in [('abergin', '3472'),
                       ('mtyler', '2701'),
                       ('ptyler', '0327'),
                       ('gtyler', '0312'),
                       ('ewalker', '2295'),
                       ('awalker', '0061'),
                       ('jreuter', '2942'),
                       ('passin', '3530')
                       ]:
        create_users_all(users_people, user, room, 'Cupertino',
                         'Not the one you looking for.',
                         '123')

    for user, name, lang, tele in [
            (f'uid=user147,ou=Çlose Crèkä,{DEFAULT_SUFFIX}', 'Ellàdiñé Passin',
             'lang-de', '+1 408 555 7393'),
            (f'uid=user0, ou=Ännheimè,{DEFAULT_SUFFIX}', 'Babette Rynders',
             'lang-es', '+1 415 788-4115'),
            (f'uid=user1,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', 'myrty DeCoursin',
             'lang-ie', '+1 408 689-8883'),
            (f'uid=user2,ou=Çéliné Ändrè,{DEFAULT_SUFFIX}', "Row O'Conner",
             'lang-it', '+1 714 902-8784'),
            (f'uid=user10,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Candide Ruiz",
             'lang-be', '+1 818 774-5666'),
            (f'uid=user11,ou=Çéliné Ändrè,{DEFAULT_SUFFIX}', "Rosene Tarquinio",
             'lang-ie', '+1 818 512-5483'),
            (f'uid=user22,ou=Çéliné Ändrè,{DEFAULT_SUFFIX}', "Drusie Dynie",
             'lang-it', '+1 303 520-7607'),
            (f'uid=user32,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Deat Liverman",
             'lang-it', '+1 714 986-7403'),
            (f'uid=user42,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Emyd Artzer",
             'lang-be', '+1 415 382-3440'),
            (f'uid=user52,ou=Ännheimè,{DEFAULT_SUFFIX}', "Lurlene Christie",
             'lang-se', '+1 818 301-7281'),
            (f'uid=user62,ou=Çlose Crèkä,{DEFAULT_SUFFIX}', "Goutam Sawchuk",
             'lang-es', '+1 804 159-3054'),
            (f'uid=user74,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Sally Rossi",
             'lang-de', '+1 714 558-4165'),
            (f'uid=user93,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Dolores Markovic",
             'lang-it', '+1 408 374-9555'),
            (f'uid=user102,ou=Çlose Crèkä,{DEFAULT_SUFFIX}', "Clovis Safah",
             'lang-de', '+1 415 964-2124'),
            (f'uid=user115,ou=Ännheimè,{DEFAULT_SUFFIX}', "Angelie Mirande",
             'lang-ie', '+1 804 832-8156'),
            (f'uid=user127,ou=Sàn Fråncêscô,{DEFAULT_SUFFIX}', "Sibilla Millspaugh",
             'lang-it', '+1 818 204-6815')]:
        users_people.create(properties={
            'mail': f'{user}'.split(',')[0].split('=')[1] + '@redhat.com',
            'uid': f'{user}'.split(',')[0].split('=')[1],
            'cn': name,
            'sn': name.split()[1],
            'givenName': f'{user}'.split(',')[0].split('=')[1].title(),
            f'givenName;{lang}': f'{user}'.split(',')[0].split('=')[1].title(),
            f'cn;{lang}': name,
            f'sn;{lang}': name.split()[1],
            'manager': user,
            'roomnumber': '0056',
            'telephonenumber': tele,
            'userpassword': PW_DM,
            'homeDirectory': '/home/' + f'{user}'.split(',')[0].split('=')[1],
            'uidNumber': '1000',
            'gidNumber': '2000',
            'description': 'This is xman * attribute value'
        })

    users_people.create(properties={
        'l': 'Sunnyvale',
        'cn': 'Kirsten Vaughan',
        'sn': 'Vaughan',
        'givenname': 'Kirsten',
        'uid': 'kvaughan',
        'mail': 'kvaughan@example.com',
        'roomnumber': '2871',
        'nsSizeLimit': '-1',
        'nsTimeLimit': '-1',
        'nsIdleTimeout': '-1',
        'manager': f'uid=kvaughan,ou=People,{DEFAULT_SUFFIX}',
        'userpassword': PW_DM,
        'homeDirectory': '/home/' + 'kvaughan',
        'uidNumber': '1000',
        'gidNumber': '2000',
    })


@pytest.mark.parametrize("real_value", VALUES)
def test_telephone(topo, _create_entries, real_value):
    """Test telephone number attr with filter

        :id: abe3e6de-9eec-11e8-adf0-8c16451d917b
        :parametrized: yes
        :setup: Standalone
        :steps:
            1. Pass filter rules as per the condition .
        :expectedresults:
            2. Pass
        """
    conn = UserAccount(topo.standalone, f'uid=jreuter,ou=People,{DEFAULT_SUFFIX}').bind(PW_DM)
    for user in Accounts(conn, DEFAULT_SUFFIX).filter(real_value):
        assert user.get_attr_val_utf8("telephoneNumber")


@pytest.mark.parametrize("real_value", POSITIVE)
def test_all_positive(topo, _create_entries, real_value):
    """Test filters with positive output.

        :id: abe3e6dd-9ecc-11e8-adf0-8c16451d917b
        :parametrized: yes
        :setup: Standalone
        :steps:
            1. Pass filter rules as per the condition .
        :expectedresults:
            1. Pass
        """
    conn = UserAccount(topo.standalone, f'uid=tclow,ou=People,{DEFAULT_SUFFIX}').bind(PW_DM)
    assert Accounts(conn, DEFAULT_SUFFIX).filter(real_value)


@pytest.mark.parametrize("real_value", NEGATIVE)
def test_all_negative(topo, _create_entries, real_value):
    """Test filters which will not give any output.

        :id: abe3e1de-9ecc-11e8-adf0-8c16451d917b
        :parametrized: yes
        :setup: Standalone
        :steps:
            1. Pass filter rules as per the negative condition .
        :expectedresults:
            1. Fail
        """
    conn = UserAccount(topo.standalone, f'uid=tclow,ou=People,{DEFAULT_SUFFIX}').bind(PW_DM)
    assert not Accounts(conn, DEFAULT_SUFFIX).filter(real_value)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

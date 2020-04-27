# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Lib389 python ldap extended operation controls

These should be upstreamed if possible.
"""

from ldap.extop import ExtendedRequest, ExtendedResponse
from pyasn1.type import namedtype, univ
from pyasn1.codec.ber import encoder, decoder

# Tag id's should match https://www.obj-sys.com/asn1tutorial/node124.html

class LdapSSOTokenRequestValue(univ.Sequence):
    pass

class LdapSSOTokenResponseValue(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('ValidLifeTime',  univ.Integer()),
        namedtype.NamedType('EncryptedToken', univ.OctetString()),
    )

class LdapSSOTokenRequest(ExtendedRequest):
    def __init__(self, requestValidLifeTime=0):
        self.requestName = '2.16.840.1.113730.3.5.14'

    def encodedRequestValue(self):
        v = LdapSSOTokenRequestValue()
        return encoder.encode(v)

class LdapSSOTokenResponse(ExtendedResponse):
    def __init__(self, encodedResponseValue):
        self.responseName = '2.16.840.1.113730.3.5.15'
        self.responseValue = self.decodeResponseValue(encodedResponseValue)

    def decodeResponseValue(self, value):
        response_value, _ = decoder.decode(value,asn1Spec=LdapSSOTokenResponseValue())
        self.validLifeTime = int(response_value.getComponentByName('ValidLifeTime'))
        self.token = str(response_value.getComponentByName('EncryptedToken'))
        return (self.validLifeTime, self.token)


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
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('ValidLifeTime',  univ.Integer().subtype(
                implicitTag=tag.Tag(tag.tagClassUniversal,tag.tagFormatSimple,0)
            )
        ),
    )

class LdapSSOTokenResponseValue(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('ValidLifeTime',  univ.Integer().subtype(
                implicitTag=tag.Tag(tag.tagClassUniversal,tag.tagFormatSimple,2)
            )
        ),
        namedtype.NamedType('EncryptedToken', univ.OctetString().subtype(
                implicitTag=tag.Tag(tag.tagClassUniversal,tag.tagFormatSimple,4)
            )
        ),
    )

class LdapSSOTokenRequest(ExtendedRequest):
    requestName = '2.16.840.1.113730.3.5.14'
    def __init__(self, requestValidLifeTime=0):
        self.requestValidLifeTime = requestValidLifeTime

    def encodedRequestValue(self):
        v = LdapSSOTokenRequestValue()
        v.setComponentByName('ValidLifeTime', univ.Integer(self.requestValidLifeTime).subtype(
                implicitTag=tag.Tag(tag.tagClassUniversal,tag.tagFormatSimple,0)
            )
        )
        return encoder.encode(v)

class LdapSSOTokenResponse(ExtendedResponse):
    responseName = '2.16.840.1.113730.3.5.15'

    def decodeResponseValue(self, value):
        response_value, _ = decoder.decode(value,asn1Spec=LdapSSOTokenResponseValue())
        self.validLifeTime = int(response_value.getComponentByName('ValidLifeTime'))
        self.token = response_value.getComponentByName('EncryptedToken')
        return (self.validLifeTime, self.token)

class LdapSSOTokenRevokeRequest(ExtendedRequest):
    requestName = '2.16.840.1.113730.3.5.16'


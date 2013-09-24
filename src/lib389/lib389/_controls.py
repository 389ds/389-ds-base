# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Lib389 python ldap request controls.

These should be upstreamed into python ldap when possible.
"""

from ldap.controls import LDAPControl
from pyasn1.type import namedtype, univ, tag
from pyasn1.codec.ber import encoder, decoder
from pyasn1_modules.rfc2251 import AttributeDescription, LDAPDN, AttributeValue
from lib389._constants import *

# Could use AttributeDescriptionList

"""
 controlValue ::= SEQUENCE OF derefSpec DerefSpec

 DerefSpec ::= SEQUENCE {
     derefAttr       attributeDescription,    ; with DN syntax
     attributes      AttributeList }

 AttributeList ::= SEQUENCE OF attr AttributeDescription

 Needs to be matched by ber_scanf(ber, "{a{v}}", ... )
"""


class AttributeList(univ.SequenceOf):
    componentType = AttributeDescription()


class DerefSpec(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('derefAttr', AttributeDescription()),
        namedtype.NamedType('attributes', AttributeList()),
    )


class DerefControlValue(univ.SequenceOf):
    componentType = DerefSpec()

"""
 controlValue ::= SEQUENCE OF derefRes DerefRes

 DerefRes ::= SEQUENCE {
     derefAttr       AttributeDescription,
     derefVal        LDAPDN,
     attrVals        [0] PartialAttributeList OPTIONAL }

 PartialAttributeList ::= SEQUENCE OF
                partialAttribute PartialAttribute
"""


class Vals(univ.SetOf):
    componentType = AttributeValue()


class PartialAttribute(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('type', AttributeDescription()),
        namedtype.NamedType('vals', Vals()),
    )


class PartialAttributeList(univ.SequenceOf):
    componentType = PartialAttribute()
    tagSet = univ.Sequence.tagSet.tagImplicitly(tag.Tag(tag.tagClassContext,
                                                tag.tagFormatConstructed, 0))


class DerefRes(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('derefAttr', AttributeDescription()),
        namedtype.NamedType('derefVal', LDAPDN()),
        namedtype.OptionalNamedType('attrVals', PartialAttributeList()),
    )


class DerefResultControlValue(univ.SequenceOf):
    componentType = DerefRes()


class DereferenceControl(LDAPControl):
    """
    Dereference Control
    """

    def __init__(self, criticality=True, deref=None):
        LDAPControl.__init__(self, CONTROL_DEREF, criticality)
        self.deref = deref

    def encodeControlValue(self):
        cv = DerefControlValue()
        cvi = 0
        for derefSpec in self.deref.split(';'):
            derefAttr, attributes = derefSpec.split(':')
            attributes = attributes.split(',')
            al = AttributeList()
            i = 0
            while len(attributes) > 0:
                al.setComponentByPosition(i, attributes.pop())
                i += 1
            ds = DerefSpec()
            ds.setComponentByName('derefAttr', derefAttr)
            ds.setComponentByName('attributes', al)
            cv.setComponentByPosition(cvi, ds)
            cvi += 1
        return encoder.encode(cv)

    def decodeControlValue(self, encodedControlValue):
        self.entry = []
        # debug.setLogger(debug.flagAll)
        decodedValue, _ = decoder.decode(encodedControlValue,
                                         asn1Spec=DerefResultControlValue())

        for derefres in decodedValue:
            result = {}
            result['derefAttr'] = str(derefres.getComponentByName('derefAttr'))
            result['derefVal'] = str(derefres.getComponentByName('derefVal'))
            result['attrVals'] = []
            for attrval in derefres.getComponentByName('attrVals'):
                av = {}
                av['type'] = str(attrval.getComponentByName('type'))
                av['vals'] = []
                for val in attrval.getComponentByName('vals'):
                    av['vals'].append(str(val))
                result['attrVals'].append(av)
            self.entry.append(result)

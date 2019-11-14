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

from ldap.controls import (LDAPControl, RequestControl, ResponseControl)
from pyasn1.type import namedtype, univ, tag, namedval, constraint
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
        for derefSpec in self.deref.decode('utf-8').split(';'):
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


#    SortKeyList ::= SEQUENCE OF SEQUENCE {
#                     attributeType   AttributeDescription,
#                     orderingRule    [0] MatchingRuleId OPTIONAL,
#                     reverseOrder    [1] BOOLEAN DEFAULT FALSE }


class SortKeyType(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('attributeType', univ.OctetString()),
        namedtype.OptionalNamedType('orderingRule',
                                    univ.OctetString().subtype(
                                        implicitTag=tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 0)
                                    )
                                    ),
        namedtype.DefaultedNamedType('reverseOrder', univ.Boolean(False).subtype(
            implicitTag=tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 1))))


class SortKeyListType(univ.SequenceOf):
    componentType = SortKeyType()


class SSSRequestControl(RequestControl):
    '''Order result server side
        >>> s = SSSRequestControl('-cn')
    '''
    controlType = '1.2.840.113556.1.4.473'

    def __init__(
            self,
            criticality=False,
            ordering_rules=None,
    ):
        RequestControl.__init__(self, self.controlType, criticality)
        self.ordering_rules = ordering_rules
        if isinstance(ordering_rules, str):
            ordering_rules = [ordering_rules]
        for rule in ordering_rules:
            rule = rule.split(':')
            assert len(rule) < 3, 'syntax for ordering rule: [-]<attribute-type>[:ordering-rule]'

    def asn1(self):
        p = SortKeyListType()
        for i, rule in enumerate(self.ordering_rules):
            q = SortKeyType()
            reverse_order = rule.startswith('-')
            if reverse_order:
                rule = rule[1:]
            if ':' in rule:
                attribute_type, ordering_rule = rule.split(':')
            else:
                attribute_type, ordering_rule = rule, None
            q.setComponentByName('attributeType', attribute_type)
            if ordering_rule:
                q.setComponentByName('orderingRule', ordering_rule)
            if reverse_order:
                q.setComponentByName('reverseOrder', 1)
            p.setComponentByPosition(i, q)
        return p

    def encodeControlValue(self):
        return encoder.encode(self.asn1())


class SortResultType(univ.Sequence):
    componentType = namedtype.NamedTypes(
        namedtype.NamedType('sortResult', univ.Enumerated().subtype(
            namedValues=namedval.NamedValues(
                ('success', 0),
                ('operationsError', 1),
                ('timeLimitExceeded', 3),
                ('strongAuthRequired', 8),
                ('adminLimitExceeded', 11),
                ('noSuchAttribute', 16),
                ('inappropriateMatching', 18),
                ('insufficientAccessRights', 50),
                ('busy', 51),
                ('unwillingToPerform', 53),
                ('other', 80)),
            subtypeSpec=univ.Enumerated.subtypeSpec + constraint.SingleValueConstraint(
                0, 1, 3, 8, 11, 16, 18, 50, 51, 53, 80))),
        namedtype.OptionalNamedType('attributeType',
                                    univ.OctetString().subtype(
                                        implicitTag=tag.Tag(tag.tagClassContext, tag.tagFormatSimple, 0)
                                    )
                                    ))


class SSSResponseControl(ResponseControl):
    controlType = '1.2.840.113556.1.4.474'

    def __init__(self, criticality=False):
        ResponseControl.__init__(self, self.controlType, criticality)

    def decodeControlValue(self, encoded):
        p, rest = decoder.decode(encoded, asn1Spec=SortResultType())
        assert not rest, 'all data could not be decoded'
        self.result = int(p.getComponentByName('sortResult'))
        self.result_code = p.getComponentByName('sortResult').prettyOut(self.result)
        self.attribute_type_error = p.getComponentByName('attributeType')


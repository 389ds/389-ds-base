# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.domain import Domain
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older

import ldap

pytestmark = pytest.mark.tier1

INVALID = [('test_targattrfilters_1',
            f'(targattrfilters ="add=title:title=fred),del=cn:(cn!=harry)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_2',
            f'(targattrfilters ="add=:(title=fred),del=cn:(cn!=harry)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_3',
            f'(targattrfilters ="add=:(title=fred),del=cn:(cn!=harry))'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_4',
            f'(targattrfilters ="add=title:(title=fred),=cn:(cn!=harry")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_5',
            f'(targattrfilters ="add=title:(|(title=fred)(cn=harry)),del=cn:(cn=harry)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_6',
            f'(targattrfilters ="add=title:(|(title=fred)(title=harry)),del=cn:(title=harry)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_7',
            f'(targattrfilters ="add=title:(cn=architect), '
            f'del=title:(title=architect) && l:(l=cn=Meylan,dc=example,dc=com")")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_8',
            f'(targattrfilters ="add=title:(cn=architect)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_9',
            f'(targattrfilters ="add=title:(cn=arch*)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_10',
            f'(targattrfilters ="add=title:(cn >= 1)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_11',
            f'(targattrfilters ="add=title:(cn <= 1)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_12',
            f'(targattrfilters ="add=title:(cn ~= 1)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_13',
            f'(targattrfilters ="add=title:(!(cn ~= 1))")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_14',
            f'(targattrfilters ="add=title:(&(cn=fred)(cn ~= 1))")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_15',
            f'(targattrfilters ="add=title:(|(cn=fred)(cn ~= 1))")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_16',
            f'(targattrfilters ="add=title:(&(|(title=fred)(title=harry))(cn ~= 1))")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_17',
            f'\(targattrfilters ="add=title:(&(|(&(title=harry)(title=fred))'
            f'(title=harry))(title ~= 1))")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_19',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI";  deny(write)gropdn="ldap:///anyone";)'),
           ('test_targattrfilters_21',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI";  deny(rite)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_22',
            f'(targt = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI";  deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_targattrfilters_23',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI";   absolute (all)userdn="ldap:///anyone";)'),
           ('test_Missing_acl_mispel',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0; alc "Name of the ACI";  deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Missing_acl_string',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 3.0;  "Name of the ACI";  deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Wrong_version_string',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(version 2.0; acl "Name of the ACI";  deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Missing_version_string',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr="*")'
            f'(; acl "Name of the ACI";  deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Authenticate_statement',
            f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
            f'(targetattr != "uid")'
            f'(targetattr="*")(version 3.0; acl "Name of the ACI";  deny absolute (all)'
            f'userdn="ldap:///anyone";)'),
           ('test_Multiple_targets',
            f'(target = ldap:///ou=Product Development,{DEFAULT_SUFFIX})'
            f'(target = ldap:///ou=Product Testing,{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Target_set_to_self',
            f'(target = ldap:///self)(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_target_set_with_ldap_instead_of_ldap',
            f'(target = ldap:\\\{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_target_set_with_more_than_three',
            f'(target = ldap:////{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_target_set_with_less_than_three',
            f'(target = ldap://{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_bind_rule_set_with_less_than_three',
            f'(target = ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:/anyone";)'),
           ('test_Use_semicolon_instead_of_comma_in_permission',
            f'(target = ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny '
            f'(read; search; compare; write)userdn="ldap:///anyone";)'),
           ('test_Use_double_equal_instead_of_equal_in_the_target',
            f'(target == ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_use_double_equal_instead_of_equal_in_user_and_group_access',
            f'(target = ldap:///{DEFAULT_SUFFIX})'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)'
            f'userdn == "ldap:///anyone";)'),
           ('test_donot_cote_the_name_of_the_aci',
            f'(target = ldap:///{DEFAULT_SUFFIX})'
            f'(version 3.0; acl  Name of the ACI ; deny absolute (all)userdn = "ldap:///anyone";)'),
           ('test_extra_parentheses_case_1',
            f'( )(target = ldap:///{DEFAULT_SUFFIX}) (targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn = "ldap:///anyone";)'),
           ('test_extra_parentheses_case_2',
            f'(((((target = ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)'
            f'userdn == "ldap:///anyone";)'),
           ('test_extra_parentheses_case_3',
            f'(((target = ldap:///{DEFAULT_SUFFIX}) (targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute '
            f'(all)userdn = "ldap:///anyone";)))'),
           ('test_no_semicolon_at_the_end_of_the_aci',
            f'(target = ldap:///{DEFAULT_SUFFIX}) (targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn = "ldap:///anyone")'),
           ('test_a_character_different_of_a_semicolon_at_the_end_of_the_aci',
            f'(target = ldap:///{DEFAULT_SUFFIX}) (targetattr="*")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn = "ldap:///anyone"%)'),
           ('test_bad_filter',
            f'(target = ldap:///{DEFAULT_SUFFIX}) '
            f'(targetattr="cn |&| sn |(|) uid")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn = "ldap:///anyone";)'),
           ('test_Use_double_equal_instead_of_equal_in_the_targattrfilters',
            f'(target = ldap:///{DEFAULT_SUFFIX})(targattrfilters== "add=title:(title=architect)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
           ('test_Use_double_equal_instead_of_equal_inside_the_targattrfilters',
            f'(target = ldap:///{DEFAULT_SUFFIX})(targattrfilters="add==title:(title==architect)")'
            f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),]


FAILED = [('test_targattrfilters_18',
           f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
           f'(targetattr="*")'
           f'(version 3.0; acl "Name of the ACI";  deny(write)userdn="ldap:///{"123" * 300}";)'),
          ('test_targattrfilters_20',
           f'(target = ldap:///cn=Jeff Vedder,ou=Product Development,{DEFAULT_SUFFIX})'
           f'(targetattr="*")'
           f'(version 3.0; acl "Name of the ACI";  deny(write)userdns="ldap:///anyone";)'),
          ('test_bind_rule_set_with_more_than_three',
           f'(target = ldap:///{DEFAULT_SUFFIX})(targetattr="*")'
           f'(version 3.0; acl "Name of the ACI"; deny absolute (all)'
           f'userdn="ldap:////////anyone";)'),
          ('test_Use_double_equal_instead_of_equal_in_the_targetattr',
           f'(target = ldap:///{DEFAULT_SUFFIX})(targetattr==*)'
           f'(version 3.0; acl "Name of the ACI"; deny absolute (all)userdn="ldap:///anyone";)'),
          ('test_Use_double_equal_instead_of_equal_in_the_targetfilter',
           f'(target = ldap:///{DEFAULT_SUFFIX})(targetfilter==*)'
           f'(version 3.0; acl "Name of the ACI"; deny absolute '
           f'(all)userdn="ldap:///anyone";)'), ]


@pytest.mark.xfail(reason='https://bugzilla.redhat.com/show_bug.cgi?id=1691473')
@pytest.mark.parametrize("real_value", [a[1] for a in FAILED],
                         ids=[a[0] for a in FAILED])
def test_aci_invalid_syntax_fail(topo, real_value):
    """Try to set wrong ACI syntax.

        :id: 83c40784-fff5-49c8-9535-7064c9c19e7e
        :parametrized: yes
        :setup: Standalone Instance
        :steps:
            1. Create ACI
            2. Try to setup the ACI with Instance
        :expectedresults:
            1. It should pass
            2. It should not pass
        """
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    with pytest.raises(ldap.INVALID_SYNTAX):
        domain.add("aci", real_value)


@pytest.mark.parametrize("real_value", [a[1] for a in INVALID],
                         ids=[a[0] for a in INVALID])
def test_aci_invalid_syntax(topo, real_value):
    """Try to set wrong ACI syntax.

        :id: e8bf20b6-48be-4574-8300-056e42a0f0a8
        :parametrized: yes
        :setup: Standalone Instance
        :steps:
            1. Create ACI
            2. Try to setup the ACI with Instance
        :expectedresults:
            1. It should pass
            2. It should not pass
        """
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    with pytest.raises(ldap.INVALID_SYNTAX):
        domain.add("aci", real_value)


def test_target_set_above_the_entry_test(topo):
    """
        Try to set wrong ACI syntax.

        :id: d544d09a-6ed1-11e8-8872-8c16451d917b
        :setup: Standalone Instance
        :steps:
            1. Create ACI
            2. Try to setup the ACI with Instance
        :expectedresults:
            1. It should pass
            2. It should not pass
    """
    domain = Domain(topo.standalone, "ou=People,{}".format(DEFAULT_SUFFIX))
    with pytest.raises(ldap.INVALID_SYNTAX):
        domain.add("aci", f'(target = ldap:///{DEFAULT_SUFFIX})'
                          f'(targetattr="*")(version 3.0; acl "Name of the ACI"; deny absolute '
                          f'(all)userdn="ldap:///anyone";)')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

"""
   :Requirement: 389-ds-base: Memberof Plugin
"""

def check_membership(user, group_dn, find_result=True):
    """Check if a user has memberOf attribute for the specified group"""
    memberof_values = user.get_attr_vals_utf8_l('memberof')
    found = group_dn.lower() in memberof_values
    
    if find_result:
        assert found, f"User {user.dn} should be a member of {group_dn}"
    else:
        assert not found, f"User {user.dn} should NOT be a member of {group_dn}"
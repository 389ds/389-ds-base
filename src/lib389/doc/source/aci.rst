ACI
==========

Usage example
------------------
::

    ACI_TARGET = ('(targetfilter ="(ou=groups)")(targetattr ="uniqueMember '
                  '|| member")')
    ACI_ALLOW = ('(version 3.0; acl "Allow test aci";allow (read, search, '
                 'write)')
    ACI_SUBJECT = ('(userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" '
                   'and userdn="ldap:///dc=example,dc=com??sub?(manager=uid='
                   'wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=examp'
                   'le,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=exampl'
                   'e,dc=com)" );)')
     
    # Add some entry with ACI
    group_dn = 'cn=testgroup,{}'.format(DEFAULT_SUFFIX)
    gentry = Entry(group_dn)
    gentry.setValues('objectclass', 'top', 'extensibleobject')
    gentry.setValues('cn', 'testgroup')
    gentry.setValues('aci', ACI_BODY)
    standalone.add_s(gentry)

    # Get and parse ACI
    acis = standalone.aci.list()
    aci = acis[0]
     
    assert aci.acidata == {
        'allow': [{'values': ['read', 'search', 'write']}],
        'target': [], 'targetattr': [{'values': ['uniqueMember', 'member'],
                                      'equal': True}],
        'targattrfilters': [],
        'deny': [],
        'acl': [{'values': ['Allow test aci']}],
        'deny_raw_bindrules': [],
        'targetattrfilters': [],
        'allow_raw_bindrules': [{'values': [(
            'userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and'
            ' userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,'
            'ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com'
            '??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" ')]}],
        'targetfilter': [{'values': ['(ou=groups)'], 'equal': True}],
        'targetscope': [],
        'version 3.0;': [],
        'rawaci': complex_aci
    }
     
    # You can get a raw ACI
    raw_aci = aci.getRawAci()

Additional information about ACI
----------------------------------

- https://access.redhat.com/documentation/en-US/Red_Hat_Directory_Server/10/html/Administration_Guide/Managing_Access_Control-Bind_Rules.html
- https://access.redhat.com/documentation/en-US/Red_Hat_Directory_Server/10/html/Administration_Guide/Managing_Access_Control-Creating_ACIs_Manually.html

Module documentation
-----------------------

.. autoclass:: lib389.aci.Aci
   :members:

.. autoclass:: lib389._entry.EntryAci
   :members:

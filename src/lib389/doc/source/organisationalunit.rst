Organisational Unit
====================

Usage example
--------------
::

    # Don't forget that Services requires created rdn='ou=Services'
    # This you can create with OrganisationalUnits
     
    from lib389.idm.organisationalunit import OrganisationalUnits
    from lib389.idm.services import ServiceAccounts
     
    ous = OrganisationalUnits(standalone, DEFAULT_SUFFIX)
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
     
    # Create the OU for them
    ous.create(properties={
            'ou': 'Services',
            'description': 'Computer Service accounts which request DS bind',
        })
     
    # Now, we can create the services from here.
    service = services.create(properties={
        'cn': 'testbind',
        'userPassword': 'Password1'
        })
     
    conn = service.bind('Password1')
    conn.unbind_s()


Module documentation
-----------------------

.. autoclass:: lib389.idm.organisationalunit.OrganisationalUnits
   :members:

.. autoclass:: lib389.idm.organisationalunit.OrganisationalUnit
   :members:


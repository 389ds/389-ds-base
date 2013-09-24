DSE ldif
==========

Usage example
--------------
::

    from lib389.dseldif import DSEldif
     
    dse_ldif = DSEldif(topo.standalone)
     
    # Get a list of attribute values under a given entry
    config_cn = dse_ldif.get(DN_CONFIG, 'cn')
     
    # Add an attribute under a given entry
    dse_ldif.add(DN_CONFIG, 'someattr', 'someattr_value')
     
    # Replace attribute values with a new one under a given entry. It will remove all previous 'someattr' values
    dse_ldif.replace(DN_CONFIG, 'someattr', 'someattr_value')
     
    # Delete attributes under a given entry
    dse_ldif.delete(DN_CONFIG, 'someattr')
    dse_ldif.delete(DN_CONFIG, 'someattr', 'someattr_value')


Module documentation
-----------------------

.. autoclass:: lib389.dseldif.DSEldif
   :members:

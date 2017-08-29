Monitor
==========

Usage example
--------------
::

    # Monitor and MonitorLDBM are the simple DSLdapObject things.
    # You can use all methods from chapter above to get current server performance detail
    version = standalone.monitor.get_attr_val('version')
    dbcachehit = standalone.monitorldbm.get_attr_val('dbcachehit')


Module documentation
-----------------------

.. autoclass:: lib389.monitor.Monitor
   :members:


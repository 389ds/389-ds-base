Replica
==========

Usage example
--------------
::

    from lib389.replica import Replicas
     
    replicas = Replicas(standalone)
    # Enable replication
    # - changelog will be created
    # - replica manager will be with the defaults
    # - replica.create() will be executed
    replica = replicas.enable(suffix=DEFAULT_SUFFIX,
                              role=REPLICAROLE_MASTER,
                              replicaID=REPLICAID_MASTER_1)
    # Roles - REPLICAROLE_MASTER, REPLICAROLE_HUB, and REPLICAROLE_CONSUMER
    # For masters and hubs you can use the constants REPLICAID_MASTER_X and REPLICAID_HUB_X
    # Change X for a number from 1 to 100 - for role REPLICAROLE_MASTER only
     
    # Disable replication
    # - agreements and replica entry will be deleted
    # - changelog is not deleted (but should?)
    replicas.disable(suffix=DEFAULT_SUFFIX)
      
    # Get RUV entry
    replicas.get_ruv_entry()
     
    # Get DN
    replicas.get_dn(suffix)
     
    # Promote
    replicas.promote(suffix=DEFAULT_SUFFIX,
                     newrole=REPLICAROLE_MASTER,
                     binddn=REPL_BINDDN,
                     rid=REPLICAID_MASTER_1)
    # Demote
    replicas.demote(suffix=DEFAULT_SUFFIX,
                    newrole=REPLICAROLE_CONSUMER)
    # Test, that replication works
    replicas.test(master2)
     
    # Additional replica object methods
    # Get role
    replica.get_role()
     
    replica.deleteAgreements()

Module documentation
-----------------------

.. autoclass:: lib389.replica.Replicas
   :members:
   :inherited-members:

.. autoclass:: lib389.replica.Replica
   :members:
   :inherited-members:

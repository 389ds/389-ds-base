Replica
==========


Usage example
--------------

Basically, when you want a simple replica configuration without any hubs, you can use create_topology function.
In more complex cases you have to use our Replica API to build your own topology exactly the way you want it.
Still, it is better if you'll use the 'create_topology' method for basic initial setup and then you can continue to expand it.

  ::

    from lib389.topologies import create_topology

    topology = create_topology({ReplicaRole.SUPPLIER: 2,
                                ReplicaRole.CONSUMER: 2})


For basic Replica operations (the rest in the docs bellow):

  ::

    from lib389.replica import Replicas

    replicas = Replicas(standalone)
    # Enable replication
    # - changelog will be created
    # - replica manager will be with the defaults
    # - replica.create() will be executed
    replica = replicas.enable(suffix=DEFAULT_SUFFIX,
                              role=ReplicaRole.SUPPLIER,
                              replicaID=REPLICAID_SUPPLIER_1)

    # Or you can get it as usual DSLdapObject
    replica = replicas.list()[0]

    # Roles - ReplicaRole.SUPPLIER, ReplicaRole.HUB, and ReplicaRole.CONSUMER
    # For masters and hubs you can use the constants REPLICAID_SUPPLIER_X and REPLICAID_HUB_X
    # Change X for a number from 1 to 100 - for role ReplicaRole.SUPPLIER only

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
                     newrole=ReplicaRole.SUPPLIER,
                     binddn=REPL_BINDDN,
                     rid=REPLICAID_SUPPLIER_1)
    # Demote
    replicas.demote(suffix=DEFAULT_SUFFIX,
                    newrole=ReplicaRole.CONSUMER)
    # Test, that replication works
    replicas.test(supplier2)

    # Additional replica object methods
    # Get role
    replica.get_role()

    replica.deleteAgreements()

Module documentation
-----------------------

.. autoclass:: lib389.replica.ReplicationManager
   :members:
   :inherited-members:

.. autoclass:: lib389.replica.RUV
   :members:
   :inherited-members:

.. autoclass:: lib389.replica.Replicas
   :members:
   :inherited-members:

.. autoclass:: lib389.replica.Replica
   :members:
   :inherited-members:

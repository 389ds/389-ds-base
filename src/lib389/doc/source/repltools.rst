Replication Tools
==================

Usage example
--------------
::

    from lib389.repltools import ReplTools

    # Gather all the CSN strings from the access and verify all of those CSNs exist on all the other replicas.
    # dirsrv_replicas - a list of DirSrv objects.  The list must begin with supplier replicas
    # ignoreCSNs - an optional string of csns to be ignored
    # if the caller knows that some csns can differ eg.: '57e39e72000000020000|vucsn-57e39e76000000030000'
    ReplTools.checkCSNs([supplier1, supplier2], ignoreCSNs=None)

    # Find and measure the convergence of entries from a replica, and
    # print a report on how fast all the "ops" replicated to the other replicas.
    # suffix - Replicated suffix
    # ops - A list of "operations" to search for in the access logs
    # replica - Dirsrv object where the entries originated
    # all_replicas - A list of Dirsrv replicas
    # It returns - The longest time in seconds for an operation to fully converge
    longest_time = ReplTools.replConvReport(DEFAULT_SUFFIX, ops, supplier1, [supplier1, supplier2])

    # Take a list of DirSrv Objects and check to see if all of the present
    # replication agreements are idle for a particular backend
    assert(ReplTools.replIdle([supplier1, supplier2], suffix=DEFAULT_SUFFIX))
    defaultProperties = {
                REPLICATION_BIND_DN: "cn=replication manager,cn=config",
                REPLICATION_BIND_PW

    # Create an entry that will be used to bind as replication manager
    ReplTools.createReplManager(standalone,
                                repl_manager_dn=defaultProperties[REPLICATION_BIND_DN],
                                repl_manager_pw=defaultProperties[REPLICATION_BIND_PW])


Module documentation
-----------------------

.. autoclass:: lib389.repltools.ReplTools
   :members:


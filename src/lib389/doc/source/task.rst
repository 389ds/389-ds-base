Indexes
==========

Besides the predefined tasks (which described in a chapter below) you can create
your own task objects with specifying a DN (https://github.com/389ds/389-ds-base/blob/master/src/lib389/lib389/_constants.py#L146)

Usage example
--------------
::

    from lib389.tasks import Task

    newtask = Task(instance, dn) # Should we create Tasks and put the precious to TasksLegacy?

    newtask.create(rdn, properties, basedn)

    # Check if the task is complete
    assert(newtask.is_complete())

    # Check task's exit code if task is complete, else None
    if newtask.is_complete():
        exit_code = newtask.get_exit_code()

    # Wait until task is complete
    newtask.wait()

    # If True,  waits for the completion of the task before to return
    args = {TASK_WAIT: True}

    # Some tasks ca be found only under old object. You can access them with this:
    standalone.tasks.importLDIF(DEFAULT_SUFFIX, path_ro_ldif, args)
    standalone.tasks.exportLDIF(DEFAULT_SUFFIX, benamebase=None, output_file=path_to_ldif, args)
    standalone.tasks.db2bak(backup_dir, args)
    standalone.tasks.bak2db(backup_dir, args)
    standalone.tasks.reindex(suffix=None, benamebase=None, attrname=None, args)
    standalone.tasks.fixupMemberOf(suffix=None, benamebase=None, filt=None, args)
    standalone.tasks.fixupTombstones(bename=None, args)
    standalone.tasks.automemberRebuild(suffix=DEFAULT_SUFFIX, scope='sub', filterstr='objectclass=top', args)
    standalone.tasks.automemberExport(suffix=DEFAULT_SUFFIX, scope='sub', fstr='objectclass=top', ldif_out=None, args)
    standalone.tasks.automemberMap(ldif_in=None, ldif_out=None, args)
    standalone.tasks.fixupLinkedAttrs(linkdn=None, args)
    standalone.tasks.schemaReload(schemadir=None, args)
    standalone.tasks.fixupWinsyncMembers(suffix=DEFAULT_SUFFIX, fstr='objectclass=top', args)
    standalone.tasks.syntaxValidate(suffix=DEFAULT_SUFFIX, fstr='objectclass=top', args)
    standalone.tasks.usnTombstoneCleanup(suffix=DEFAULT_SUFFIX, bename=None, maxusn_to_delete=None, args)
    standalone.tasks.sysconfigReload(configfile=None, logchanges=None, args)
    standalone.tasks.cleanAllRUV(suffix=None, replicaid=None, force=None, args)
    standalone.tasks.abortCleanAllRUV(suffix=None, replicaid=None, certify=None, args)
    standalone.tasks.upgradeDB(nsArchiveDir=None, nsDatabaseType=None, nsForceToReindex=None, args)


Module documentation
-----------------------

.. autoclass:: lib389.tasks.Tasks
   :members:

.. autoclass:: lib389.tasks.Task
   :members:

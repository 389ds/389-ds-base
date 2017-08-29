Changelog
==========

Usage example
--------------
::

    standalone = topology.standalone
    # Create
    changelog_dn = standalone.changelog.create()
    # List
    changelog_entries = standalone.changelog.list(changelogdn=changelog_dn)
    # Delete
    standalone.changelog.delete()


Module documentation
----------------------

.. autoclass:: lib389.changelog.Changelog
   :members:


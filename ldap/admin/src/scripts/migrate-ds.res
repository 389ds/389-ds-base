begin_ds_migration = Beginning migration of directory server instances in %s . . .\n
end_ds_migration = Directory server migration is complete.  Please check output and log files for details.\n
migration_exiting = Exiting . . .\nLog file is '%s'\n\n
instance_already_exists = The target directory server instance already exists at %s.  Skipping migration.  Note that if you want to migrate the old instance you will have to first remove the new one of the same name.\n\n
error_reading_entry = Could not read the entry '%s'.  Error: %s\n
error_updating_merge_entry = Could not %s the migrated entry '%s' in the target directory server.  Error: %s\n
error_importing_migrated_db = Could not import the LDIF file '%s' for the migrated database.  Error: %s.  Output: %s\n
error_reading_olddbconfig = Could not read the old database configuration information.  Error: %s\n
error_migrating_schema = Could not copy old schema file '%s'.  Error: %s\n
error_copying_dbdir = Could not copy database directory '%s' to '%s'.  Error: %s\n
error_copying_dbfile = Could not copy database file '%s' to '%s'.  Error: %s\n
error_dbsrcdir_not_exist = Could not copy from the database source directory '%s' because it does not exist.  Please check your configuration.\n
error_no_instances = Could not find any instances in the old directory '%s' to migrate.\n
error_removing_temp_db_files = Could not remove the temporary db files in '%s' to clear the directory in preparation for the migrated db files.  Error: %s\n
error_copying_certdb = Could not copy the certificate database file '%s' to '%s'.  Error: %s\n
error_copying_keydb = Could not copy the private key database file '%s' to '%s'.  Error: %s\n
error_copying_secmoddb = Could not copy the security module database file '%s' to '%s'.  Error: %s\n
error_copying_pinfile = Could not copy the key database PIN file '%s' to '%s'.  Error: %s\n
error_copying_certmap = Could not copy the client certificate mapping file '%s' to '%s'.  Error: %s\n
ldif_required_for_cross_platform = No LDIF files were found in %s.\n
LDIF files are required in order to do cross platform migration.  The\
database files are not binary compatible, and the new databases must\
be initialized from an LDIF export of the old databases.  Please refer\
to the migration instructions for help with how to do this.\n\n

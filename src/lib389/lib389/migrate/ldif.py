# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


from ldif import LDIFParser

class ImportMetadata(LDIFParser):
    def __init__(self, f_import):
        self.suffix = None
        super().__init__(f_import)

    def handle(self, dn, entry):
        # This only sets on the first entry, which is the basedn
        if self.suffix is None:
            self.suffix = dn

class LdifMetadata(object):
    def __init__(self, ldifs, log):
        self.log = log
        self.log.info("Examining Ldifs ...")
        self.log.debug(ldifs)
        self.inner = {}

        # For each ldif
        for ldif in ldifs:
            with open(ldif, 'r') as f_import:
                # Open and read the first entry.
                meta = ImportMetadata(f_import)
                meta.parse()
                self.log.debug(f"{ldif} contains {meta.suffix}:{meta.records_read} entries")
                # Stash the suffix with the ldif path.
                self.inner[meta.suffix] = ldif
        self.log.info('Completed Ldif Metadata Parsing.')

    def get_suffixes(self):
        return self.inner

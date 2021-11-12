# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


from lib389._mapped_object import DSLdapObject


class RootDSE(DSLdapObject):
    """
    Check if the directory supports features or not.
    """
    def __init__(self, conn):
        """@param conn - a DirSrv instance """
        super(RootDSE, self).__init__(instance=conn)
        self._dn = ""

    def supported_sasl(self):
        return self.get_attr_vals_utf8('supportedSASLMechanisms')

    def available_sasl(self):
        return self.get_attr_vals_utf8('availableSASLMechanisms')

    def supports_sasl_gssapi(self):
        return self.present("supportedSASLMechanisms", 'GSSAPI')

    def supports_sasl_ldapssotoken(self):
        return self.present("supportedSASLMechanisms", "LDAPSSOTOKEN")

    def supports_sasl_plain(self):
        return self.present("supportedSASLMechanisms", "PLAIN")

    def supports_sasl_external(self):
        return self.present("supportedSASLMechanisms", "EXTERNAL")

    def supports_exop_whoami(self):
        return self.present("supportedExtension", "1.3.6.1.4.1.4203.1.11.3")

    def supports_exop_ldapssotoken_request(self):
        return self.present("supportedExtension", "2.16.840.1.113730.3.5.14")

    def supports_exop_ldapssotoken_revoke(self):
        return self.present("supportedExtension", "2.16.840.1.113730.3.5.16")

    def get_supported_ctrls(self):
        return self.get_attr_vals_utf8('supportedControl')


# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.account import Account
from lib389._constants import DN_DM, PW_DM


class DirectoryManager(Account):
    """
    The directory manager. This is a convinence class to help with rebinds
    to the same server, as well as some other DM related specific tasks.
    """

    def __init__(self, instance, dn=DN_DM):
        """The Directory Manager instance. Useful for binding in tests.

        :param instance: An instance
        :type instance: lib389.DirSrv
        :param dn: Entry DN
        :type dn: str
        """
        super(DirectoryManager, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = []
        self._create_objectclasses = None
        self._protected = True

    def change_password(self, new_password):
        if new_password == "":
            raise ValueError("You can not set the Directory Manager password to nothing")
        self._instance.config.set('nsslapd-rootpw', new_password)

    def bind(self, password=PW_DM, *args, **kwargs):
        """Bind as the Directory Manager. We have a default test password
        that can be overriden.

        :param password: The password to bind as for Directory Manager
        :type password: str
        :returns: A new connection bound as directory manager.
        """
        return super(DirectoryManager, self).bind(password, *args, **kwargs)

    def rebind(self, password=PW_DM):
        """Rebind on the same connection
        :param password: Directory Manager password
        :type password: str
        """
        self._instance.simple_bind_s(self.dn, password, escapehatch='i am sure')

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import logging
from typing import Optional
from lib389 import DirSrv
from lib389.nss_ssl import NssSsl
from lib389.dyncerts import DynamicCerts
from lib389.config import RSA

log = logging.getLogger(__name__)

class CertManager:
    """
    Certificate manager for 389 Directory Server.

    Automatically selects DynamicCerts backend if available via LDAPI,
    otherwise falls back to NSS DB.
    """

    def __init__(self, instance):
        """
        Initialise a CertManager object, which selects the appropriate
        certificate handler (DynamicCert or NSS).

        :param instance: DirSrv instance
        :raises ValueError: If instance is None
        """
        if not isinstance(instance, DirSrv):
            raise ValueError("A DirSrv instance is required")

        self.dirsrv = instance

        if self.dirsrv.status():
            self.cert_handler = DynamicCerts(instance=self.dirsrv)
        else:
            self.cert_handler = NssSsl(dirsrv=self.dirsrv)

        self.cert_handler_name = type(self.cert_handler).__name__

    def list_certs(self):
        """
        Return a list of all certificates exposed by the backend.

        :return: list of certificate dictionaries or tuples
        """
        return self.cert_handler.list_certs()

    def list_ca_certs(self):
        """
        Return a list of all CA certificates exposed by the backend.

        :return: list of CA certificate dictionaries or tuples
        """
        return self.cert_handler.list_ca_certs()

    def get_cert(self, nickname: str):
        """
        Get certificate details by nickname/CN.

        :param nickname: Certificate nickname
        :return: backend certificate object or None
        """
        return self.cert_handler.get_cert_details(nickname=nickname)

    def del_cert(self, nickname: str):
        """
        Delete a certificate by nickname.

        :param nickname: Certificate nickname
        :raises ValueError: If the nickname is empty.
        """
        if not nickname:
            raise ValueError("Certificate nickname cannot be empty")

        self.cert_handler.del_cert(nickname)

    def add_cert(
        self,
        cert_file: str,
        nickname: str,
        pkcs12_password: Optional[str] = None,
        primary: bool = False,
        ca: bool = False,
        force: bool = False
    ):
        """
        Add or replace a certificate.

        :param cert_file: Path to certificate file (PEM, DER, or PKCS#12)
        :param nickname: Certificate nickname/CN
        :param pkcs12_password: Password for PKCS#12, if any
        :param primary: Set as the server's primary SSL certificate
        :param ca: Whether this certificate is a CA certificate
        :param force: Force the addition of a certificate that cannot be verified
        :raises ValueError: If cert_file or nickname is invalid
        """
        if not os.path.isfile(cert_file):
            raise ValueError(f"Certificate file not found: {cert_file}")
        if not nickname:
            raise ValueError("Certificate nickname must not be empty")

        self.cert_handler.add_cert(
            nickname=nickname,
            cert_file=cert_file,
            pkcs12_password=pkcs12_password,
            ca=ca,
            force=force
        )

        if primary:
            try:
                RSA(self.dirsrv).set("nsSSLPersonalitySSL", nickname)
                log.info(f"Set certificate '{nickname}' as primary SSL certificate.")
            except Exception as e:
                log.error(f"Failed to set primary SSL cert '{nickname}': {e}")
                raise

    def add_ca_cert(self, cert_file: str, nickname: str, force: bool = False):
        """
        Add one or more CA certificates from a PEM bundle or single DER.

        :param cert_file: Path to certificate file (PEM, DER)
        :param nickname: Certificate nickname
        :raises ValueError: If file is missing
        """
        if not os.path.exists(cert_file):
            raise ValueError(f"Certificate file does not exist: {cert_file}")

        self.cert_handler.add_ca_cert(cert_file, nickname, force=force)

    def edit_cert_trust(self, nickname: str, trust_flags: str):
        """
        Edit trust flags on an existing certificate.

        :param nickname: Certificate nickname
        :param trust_flags: NSS style trust flag triplet, e.g. 'CT,,'
        """
        return self.cert_handler.edit_cert_trust(nickname=nickname, trust_flags=trust_flags)
